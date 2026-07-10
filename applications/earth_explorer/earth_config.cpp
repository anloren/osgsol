#include "earth_config.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <osg/Notify>

namespace earthcfg
{
    static std::string trimWs(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string();
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    std::string parseKeysEnv(const std::string& content, const std::string& name)
    {
        std::istringstream in(content);
        std::string line;
        while (std::getline(in, line))
        {
            std::string t = trimWs(line);
            if (t.empty() || t[0] == '#') continue;
            size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            if (trimWs(t.substr(0, eq)) == name) return trimWs(t.substr(eq + 1));
        }
        return std::string();
    }

    std::string keysEnvPath()
    {
        const char* home = getenv("HOME");
        if (home == NULL || home[0] == '\0') return std::string();
        return std::string(home) + "/Library/Application Support/EarthExplorer/keys.env";
    }

    std::string resolveKey(const char* envName)
    {
        if (envName == NULL || envName[0] == '\0') return std::string();
        const char* env = getenv(envName);
        if (env && env[0]) return std::string(env);   // 环境变量优先(可覆盖磁盘配置)
        std::string path = keysEnvPath();
        if (path.empty()) return std::string();
        std::ifstream f(path.c_str());
        if (!f) return std::string();
        std::stringstream ss; ss << f.rdbuf();
        return parseKeysEnv(ss.str(), envName);
    }

    // ---- 运行时配置注册表 -------------------------------------------------

    // 算出一项参数的初始 value(env seed 或 defVal,已钳到 [minVal,maxVal])。只返回
    // double,不构造/返回 Param——Param 内含 std::atomic<double>,不可拷贝/移动,不能
    // 按值从函数返回;调用方(params() 里的注册 lambda)拿到这个 double 后直接
    // emplace_back 在 deque 里原地构造 Param。
    //
    // 若 envName 对应的环境变量存在且能解析为数字,用它 seed 初始 value(便于老
    // EARTH_* 钩子在未接入设置面板前继续生效);否则用 defVal。envName 为空表示该
    // 参数没有对应的老钩子,直接用 defVal。
    static double seedParamValue(double defVal, double minVal, double maxVal, const char* envName)
    {
        double v = defVal;
        if (envName != NULL && envName[0] != '\0')
        {
            const char* env = getenv(envName);
            if (env != NULL && env[0] != '\0')
            {
                char* end = NULL;
                double parsed = strtod(env, &end);
                if (end != env) v = parsed;   // 只有成功解析出数字才采用,脏值忽略
            }
        }
        if (v < minVal) v = minVal;
        if (v > maxVal) v = maxVal;
        return v;
    }

    std::deque<Param>& params()
    {
        // 启动期单线程注册,运行期严禁再 emplace_back/push_back/erase(见头文件
        // Param / params() 注释:find() 返回的 Param* 依赖元素个数与地址此后不再变化)。
        static std::deque<Param> s_params = []() {
            std::deque<Param> v;
            v.emplace_back("ai.maxRounds", u8"AI 工具调用上限", u8"AI", PK_INT,
                seedParamValue(30.0, 1.0, 200.0, "EARTH_AI_MAX_ROUNDS"), 30.0, 1.0, 200.0);
            v.emplace_back("http.retries", u8"网络失败重试次数", u8"网络", PK_INT,
                seedParamValue(3.0, 0.0, 10.0, "EARTH_HTTP_RETRIES"), 3.0, 0.0, 10.0);
            v.emplace_back("badge.seconds", u8"角标显示秒数", u8"界面", PK_DOUBLE,
                seedParamValue(5.0, 0.0, 30.0, "EARTH_BADGE_SECONDS"), 5.0, 0.0, 30.0);
            return v;
        }();
        return s_params;
    }

    // 私有辅助:按 id 查找,找不到返回 nullptr。
    static Param* find(const std::string& id)
    {
        std::deque<Param>& v = params();
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].id == id) return &v[i];
        return NULL;
    }

    int getInt(const std::string& id)
    {
        Param* p = find(id);
        if (p == NULL)
        {
            OSG_WARN << "[earthcfg] getInt: unknown param id '" << id << "'" << std::endl;
            return 0;
        }
        double v = p->value.load();
        return (int)(v + (v >= 0.0 ? 0.5 : -0.5));
    }

    double getDouble(const std::string& id)
    {
        Param* p = find(id);
        if (p == NULL)
        {
            OSG_WARN << "[earthcfg] getDouble: unknown param id '" << id << "'" << std::endl;
            return 0.0;
        }
        return p->value.load();
    }

    bool getBool(const std::string& id)
    {
        Param* p = find(id);
        if (p == NULL)
        {
            OSG_WARN << "[earthcfg] getBool: unknown param id '" << id << "'" << std::endl;
            return false;
        }
        return p->value.load() != 0.0;
    }

    void setValue(const std::string& id, double v)
    {
        Param* p = find(id);
        if (p == NULL)
        {
            OSG_WARN << "[earthcfg] setValue: unknown param id '" << id << "'" << std::endl;
            return;
        }
        if (v < p->minVal) v = p->minVal;
        if (v > p->maxVal) v = p->maxVal;
        p->value.store(v);
        // TODO(持久化): 落盘到 ~/Library/Application Support/EarthExplorer/config.ini,
        // 下次启动读回;首版仅保证运行期可调 + 默认值,不做磁盘往返。
    }

    void resetDefault(const std::string& id)
    {
        Param* p = find(id);
        if (p == NULL)
        {
            OSG_WARN << "[earthcfg] resetDefault: unknown param id '" << id << "'" << std::endl;
            return;
        }
        p->value.store(p->defVal);
    }
}
