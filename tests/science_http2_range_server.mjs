#!/usr/bin/env node

import fs from 'node:fs';
import http2 from 'node:http2';

const TRANSIENT_ONCE_MODES = new Map([
    ['range-429-once', 429],
    ['range-500-once', 500],
    ['range-502-once', 502],
    ['range-503-once', 503],
    ['range-504-once', 504],
]);
const MODES = new Set([
    'success', 'range-200', 'range-200-body', 'short-range',
    'size-mismatch', 'range-503', 'range-503-exhaust',
    ...TRANSIENT_ONCE_MODES.keys(),
]);
const EXPECTED_RANGE = 'bytes=0-131071';
const PREFETCH_BYTES = 131072;

function fail(message)
{
    process.stderr.write(`science_http2_range_server: ${message}\n`);
    process.exit(2);
}

function parseArguments(argv)
{
    const values = new Map();
    for (let index = 0; index < argv.length; index += 2)
    {
        const key = argv[index];
        const value = argv[index + 1];
        if (!key?.startsWith('--') || value === undefined)
            fail('arguments must be --name value pairs');
        if (values.has(key)) fail(`duplicate argument ${key}`);
        values.set(key, value);
    }
    const required = [
        '--file', '--ready-file', '--log-file', '--cert', '--key',
        '--budget-bytes', '--mode',
    ];
    for (const key of required)
    {
        if (!values.has(key)) fail(`missing ${key}`);
    }
    const budget = Number(values.get('--budget-bytes'));
    if (!Number.isSafeInteger(budget) || budget < PREFETCH_BYTES)
        fail('--budget-bytes must be an integer of at least 131072');
    const mode = values.get('--mode');
    if (!MODES.has(mode)) fail(`unsupported mode ${mode}`);
    return {
        file: values.get('--file'),
        readyFile: values.get('--ready-file'),
        logFile: values.get('--log-file'),
        cert: values.get('--cert'),
        key: values.get('--key'),
        budget,
        mode,
    };
}

const options = parseArguments(process.argv.slice(2));
const protocol = process.env.OSGSOL_TEST_SERVER_PROTOCOL === 'http1'
    ? 'http1' : 'h2';
const fixture = fs.readFileSync(options.file);
if (fixture.length <= PREFETCH_BYTES)
    fail('fixture must be larger than the first prefetch interval');

const logFd = fs.openSync(options.logFile, 'w');
let nextSessionId = 1;
let totalAttemptedBodyBytes = 0;
let getCount = 0;
let headCount = 0;
let violation = null;
const sessions = new Set();
const activeStreamFinalizers = new Set();

function emit(event)
{
    fs.writeSync(logFd, `${JSON.stringify({
        monotonic_ns: Number(process.hrtime.bigint()),
        mode: options.mode,
        ...event,
    })}\n`);
}

function reject(stream, context, reason)
{
    violation ??= reason;
    if (!stream.closed && !stream.destroyed)
    {
        stream.respond({ ':status': 400, 'content-length': '0' });
        emit({
            event: 'response_headers',
            ...context,
            status: 400,
            content_length: 0,
            violation: reason,
        });
        stream.end();
    }
}

function sendBody(stream, context, status, headers, body, bodyLimit = body.length)
{
    if (totalAttemptedBodyBytes + bodyLimit > options.budget)
    {
        reject(stream, context, 'response body exceeds total budget');
        return;
    }
    stream.respond({ ':status': status, ...headers });
    emit({
        event: 'response_headers',
        ...context,
        status,
        content_length: Number(headers['content-length'] ?? 0),
        content_range: headers['content-range'] ?? null,
        violation: null,
    });

    let offset = 0;
    let finalized = false;
    const finalize = (aborted) =>
    {
        if (finalized) return;
        finalized = true;
        activeStreamFinalizers.delete(finalize);
        emit({
            event: 'stream_end',
            ...context,
            attempted_body_bytes: offset,
            total_attempted_body_bytes: totalAttemptedBodyBytes,
            aborted,
            violation: null,
        });
    };
    activeStreamFinalizers.add(finalize);
    const writeNext = () =>
    {
        if (stream.closed || stream.destroyed)
        {
            finalize(offset < bodyLimit);
            return;
        }
        if (offset >= bodyLimit)
        {
            stream.end();
            return;
        }
        const overflowProbe = options.mode === 'range-200-body' &&
            offset >= PREFETCH_BYTES;
        const chunkSize = Math.min(overflowProbe ? 1 : 16384,
            bodyLimit - offset);
        const chunk = body.subarray(offset, offset + chunkSize);
        stream.write(chunk, () =>
        {
            offset += chunk.length;
            totalAttemptedBodyBytes += chunk.length;
            if (context.path.includes('transport-interrupt') &&
                offset >= PREFETCH_BYTES / 2)
            {
                stream.session.destroy();
                return;
            }
            if (overflowProbe)
                setTimeout(writeNext, 50);
            else
                setImmediate(writeNext);
        });
    };
    stream.once('close', () =>
    {
        finalize(offset < bodyLimit);
    });
    writeNext();
}

function sendRangeBody(stream, context, status, headers, body,
    bodyLimit = body.length)
{
    const send = () => sendBody(
        stream, context, status, headers, body, bodyLimit);
    if (context.path.includes('head-first'))
        setTimeout(send, 200);
    else
        send();
}

const server = http2.createSecureServer({
    cert: fs.readFileSync(options.cert),
    key: fs.readFileSync(options.key),
    allowHTTP1: protocol === 'http1',
});

let nextHttp1StreamId = 1;
server.on('request', (request, response) =>
{
    if (request.httpVersionMajor !== 1) return;
    const socket = request.socket;
    if (!socket.__scienceSessionId)
    {
        socket.__scienceSessionId = `session-${nextSessionId++}`;
        emit({ event: 'session_start', session_id: socket.__scienceSessionId });
    }
    const context = {
        session_id: socket.__scienceSessionId,
        stream_id: nextHttp1StreamId++,
    };
    const method = request.method;
    const path = request.url;
    const range = request.headers.range ?? null;
    emit({ event: 'stream_start', ...context, method, path, range });

    let bodyBytes = 0;
    response.once('close', () =>
    {
        emit({
            event: 'stream_end',
            ...context,
            attempted_body_bytes: bodyBytes,
            total_attempted_body_bytes: totalAttemptedBodyBytes,
            aborted: !response.writableEnded,
            violation: null,
        });
    });
    const send = (status, headers, body = Buffer.alloc(0)) =>
    {
        response.writeHead(status, headers);
        emit({
            event: 'response_headers',
            ...context,
            status,
            content_length: Number(headers['content-length'] ?? 0),
            content_range: headers['content-range'] ?? null,
            violation: null,
        });
        bodyBytes = body.length;
        totalAttemptedBodyBytes += body.length;
        response.end(body);
    };

    if (method === 'HEAD')
    {
        ++headCount;
        setTimeout(() => send(200, {
            'accept-ranges': 'bytes',
            'content-length': String(fixture.length),
        }), 200);
        return;
    }
    if (method !== 'GET' || range !== EXPECTED_RANGE)
    {
        violation ??= method !== 'GET'
            ? `unsupported method ${method}` : `unexpected first interval ${range}`;
        send(400, { 'content-length': '0' });
        return;
    }
    ++getCount;
    send(206, {
        'content-length': String(PREFETCH_BYTES),
        'content-range': `bytes 0-${PREFETCH_BYTES - 1}/${fixture.length}`,
    }, fixture.subarray(0, PREFETCH_BYTES));
});

server.on('session', (session) =>
{
    const sessionId = `session-${nextSessionId++}`;
    session.__scienceSessionId = sessionId;
    sessions.add(session);
    emit({ event: 'session_start', session_id: sessionId });
    session.once('close', () => sessions.delete(session));
});

server.on('stream', (stream, headers) =>
{
    const context = {
        session_id: stream.session.__scienceSessionId,
        stream_id: stream.id,
        path: headers[':path'],
    };
    const method = headers[':method'];
    const path = headers[':path'];
    const range = headers.range ?? null;
    stream.on('error', (error) =>
    {
        if (options.mode !== 'range-200-body' &&
            !path.includes('transport-interrupt'))
            violation ??= `unexpected HTTP/2 stream error: ${error.message}`;
    });
    emit({ event: 'stream_start', ...context, method, path, range });

    if (method === 'HEAD')
    {
        ++headCount;
        let headEnded = false;
        const respond = () =>
        {
            if (stream.closed || stream.destroyed) return;
            let status = 200;
            const responseHeaders = {
                'accept-ranges': 'bytes',
                'content-length': String(fixture.length),
            };
            if (path.includes('head-503-exhaust'))
            {
                status = 503;
                responseHeaders['content-length'] = '0';
            }
            else if (path.includes('head-503') && headCount === 1)
            {
                status = 503;
                responseHeaders['content-length'] = '0';
            }
            else if (path.includes('head-405') && headCount === 1)
            {
                status = 405;
                responseHeaders['content-length'] = '0';
            }
            else if (path.includes('redirect-source'))
            {
                status = 302;
                responseHeaders['content-length'] = '0';
                responseHeaders.location = path.replace(
                    'redirect-source', 'redirect-target');
            }
            else if (path.includes('content-range-undersized-total'))
            {
                responseHeaders['content-length'] = String(PREFETCH_BYTES / 2);
            }
            stream.respond({ ':status': status, ...responseHeaders });
            emit({
                event: 'response_headers',
                ...context,
                status,
                content_length: Number(responseHeaders['content-length']),
                content_range: null,
                violation: null,
            });
            if (!path.includes('transport-interrupt'))
            {
                headEnded = true;
                stream.end();
            }
        };
        const delayedSuccess = options.mode === 'success' &&
            !path.includes('head-') && !path.includes('redirect-source') &&
            !path.includes('head-first') &&
            !path.includes('transport-interrupt');
        setTimeout(respond, delayedSuccess ? 200 : 0);
        stream.once('close', () =>
        {
            emit({
                event: 'stream_end',
                ...context,
                attempted_body_bytes: 0,
                total_attempted_body_bytes: totalAttemptedBodyBytes,
                aborted: !headEnded,
                violation: null,
            });
        });
        return;
    }

    if (method !== 'GET')
    {
        reject(stream, context, `unsupported method ${method}`);
        return;
    }
    ++getCount;
    if (path.includes('head-405') && typeof range !== 'string')
    {
        sendBody(stream, context, 200,
            { 'content-length': String(fixture.length) }, Buffer.alloc(0), 0);
        return;
    }
    if (typeof range !== 'string')
    {
        reject(stream, context, 'missing Range header');
        return;
    }
    if (range.includes(','))
    {
        reject(stream, context, 'comma Range rejected');
        return;
    }
    if (range !== EXPECTED_RANGE)
    {
        reject(stream, context, `unexpected first interval ${range}`);
        return;
    }

    if (path.includes('redirect-source'))
    {
        sendRangeBody(stream, context, 302, {
            'content-length': '0',
            location: path.replace('redirect-source', 'redirect-target'),
        }, Buffer.alloc(0));
        return;
    }

    if (path.includes('transport-interrupt'))
    {
        sendRangeBody(stream, context, 206, {
            'content-length': String(PREFETCH_BYTES),
            'content-range':
                `bytes 0-${PREFETCH_BYTES - 1}/${fixture.length}`,
        }, fixture, PREFETCH_BYTES);
        return;
    }

    const transientStatus = TRANSIENT_ONCE_MODES.get(options.mode);
    if (transientStatus !== undefined && getCount === 1)
    {
        sendRangeBody(stream, context, transientStatus,
            { 'content-length': '17' }, Buffer.from('transient-error!\n'));
        return;
    }
    if (options.mode === 'range-503-exhaust')
    {
        sendRangeBody(stream, context, 503,
            { 'content-length': '17' }, Buffer.from('transient-error!\n'));
        return;
    }
    if (path.includes('range-404'))
    {
        sendRangeBody(stream, context, 404,
            { 'content-length': '0' }, Buffer.alloc(0));
        return;
    }
    if (options.mode === 'range-503' && getCount === 1)
    {
        sendRangeBody(stream, context, 503,
            { 'content-length': '0' }, Buffer.alloc(0));
        return;
    }
    if (options.mode === 'range-200')
    {
        sendRangeBody(stream, context, 200,
            { 'content-length': '0' }, Buffer.alloc(0));
        return;
    }
    if (options.mode === 'range-200-body')
    {
        const attempted = Math.min(options.budget, PREFETCH_BYTES * 2);
        sendRangeBody(stream, context, 200, {}, fixture, attempted);
        return;
    }
    if (options.mode === 'short-range')
    {
        const count = PREFETCH_BYTES / 2;
        sendRangeBody(stream, context, 206, {
            'content-length': String(count),
            'content-range': `bytes 0-${count - 1}/${fixture.length}`,
        }, fixture, count);
        return;
    }

    if (path.includes('content-range-missing'))
    {
        sendRangeBody(stream, context, 206, {
            'content-length': String(PREFETCH_BYTES),
        }, fixture, PREFETCH_BYTES);
        return;
    }
    if (path.includes('content-range-malformed'))
    {
        sendRangeBody(stream, context, 206, {
            'content-length': String(PREFETCH_BYTES),
            'content-range': 'bytes malformed',
        }, fixture, PREFETCH_BYTES);
        return;
    }
    if (path.includes('content-range-spoof'))
    {
        sendRangeBody(stream, context, 206, {
            'content-length': String(PREFETCH_BYTES),
            'x-spoof': `content-range: bytes 0-${PREFETCH_BYTES - 1}/${fixture.length}`,
        }, fixture, PREFETCH_BYTES);
        return;
    }
    if (path.includes('content-range-duplicate'))
    {
        stream.additionalHeaders({
            ':status': 103,
            'content-range': `bytes 0-${PREFETCH_BYTES - 1}/${fixture.length}`,
        });
        sendRangeBody(stream, context, 206, {
            'content-length': String(PREFETCH_BYTES),
            'content-range':
                `bytes 0-${PREFETCH_BYTES - 1}/${fixture.length + 1}`,
        }, fixture, PREFETCH_BYTES);
        return;
    }

    if (path.includes('content-range-undersized-total'))
    {
        const total = PREFETCH_BYTES / 2;
        sendRangeBody(stream, context, 206, {
            'content-length': String(PREFETCH_BYTES),
            'content-range': `bytes 0-${PREFETCH_BYTES - 1}/${total}`,
        }, fixture, PREFETCH_BYTES);
        return;
    }

    const total = options.mode === 'size-mismatch' ? fixture.length + 1 : fixture.length;
    sendRangeBody(stream, context, 206, {
        'content-length': String(PREFETCH_BYTES),
        'content-range': `bytes 0-${PREFETCH_BYTES - 1}/${total}`,
    }, fixture, PREFETCH_BYTES);
});

server.on('error', (error) =>
{
    violation ??= error.message;
    process.stderr.write(`science_http2_range_server: ${error.stack}\n`);
});

server.listen(0, '127.0.0.1', () =>
{
    const address = server.address();
    fs.writeFileSync(options.readyFile, `${address.port}\n`);
});

function shutdown()
{
    for (const finalize of [...activeStreamFinalizers]) finalize(true);
    for (const session of sessions) session.destroy();
    server.close();
    fs.closeSync(logFd);
    process.exit(violation ? 1 : 0);
}

process.on('SIGTERM', shutdown);
process.on('SIGINT', shutdown);
