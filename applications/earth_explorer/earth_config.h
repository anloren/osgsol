#ifndef EARTH_CONFIG_H
#define EARTH_CONFIG_H
#include <string>
#include <deque>
#include <atomic>

// 外部凭证(API key)的持久化解析。背景:整个 app 原本所有 key(EARTH_AI_KEY /
// EARTH_FIRMS_KEY / EARTH_AISSTREAM_KEY)都只走 getenv——但 macOS 下双击 .app 启动
// 既不继承终端环境变量、也不读 ~/.zshrc,双击用户一个 key 都拿不到,必须每次从终端
// 带参数启动。这里加一个磁盘回退,让 key 配置一次、双击即用。
//
// 红线:key 只存在用户机器的 $HOME 下(仓库之外),源码里绝不硬编码任何 key 值。
namespace earthcfg
{
    // 解析 keys.env 文本:每行 NAME=VALUE,'#' 注释行与空行忽略,name/value 各自裁剪
    // 首尾空白(key 本身不含空格,但容忍用户手写时的对齐空格);value 内允许出现 '='
    // (按第一个 '=' 切分)。找不到 name 返回空串。纯函数,tests 直测。
    std::string parseKeysEnv(const std::string& content, const std::string& name);

    // keys.env 的绝对路径:$HOME/Library/Application Support/EarthExplorer/keys.env
    // (用 Application Support 而非 Caches——凭证不该放会被系统在磁盘紧张时清理的目录)。
    // $HOME 缺失时返回空串。
    std::string keysEnvPath();

    // 解析一个外部凭证:环境变量优先(保留测试/CI/临时覆盖能力),回退读 keys.env。
    // 都没有则返回空串。envName 为空指针/空串时返回空串。
    std::string resolveKey(const char* envName);

    // ---- 运行时配置注册表 -------------------------------------------------
    // 背景:后续任务(AI 工具循环上限、HTTP 重试次数、角标显示秒数……)都需要一个
    // 带默认值/取值范围的可调参数,供设置面板可视化编辑、供代码读取当前值。这里给
    // 一个最小注册表:单例 deque<Param>(选型原因见下方 params() 注释),首次调用
    // params() 时注册默认项。
    //
    // 持久化:首版不落盘(TODO:后续可加 ~/Library/Application Support/EarthExplorer/
    // config.ini 的读写),仅保证运行期可调 + 默认值 + env 一次性 seed(便于老 EARTH_*
    // 钩子在未接入设置面板前继续生效)。
    enum ParamKind { PK_INT, PK_DOUBLE, PK_BOOL };

    // 并发约定:value 会被跨线程读写——AI worker 线程读(getInt/getDouble/getBool,
    // 例如 AI 循环里读 ai.maxRounds/http.retries),设置面板 draw 线程写(setValue/
    // resetDefault)。因此 value 是 std::atomic<double>,单个标量的读写自身线程安全、
    // 无需外部加锁;但这只保证"这一个 double 的读写是原子的、可见的",不覆盖任何跨
    // 字段的复合不变量——本结构体没有这类复合逻辑(value 是唯一运行期会变的字段,
    // id/label/group/kind/defVal/minVal/maxVal 注册后只读,天然线程安全),所以单标量
    // atomic 已经足够,不需要再加锁。
    //
    // std::atomic<double> 不可拷贝、不可移动,因此 Param 本身也不可拷贝/移动(见下方
    // 显式构造函数——按值传参/返回 Param 一律无法通过编译,这是刻意的:避免有人在别处
    // 悄悄拷贝出一份 Param 快照,造成 value 出现第二个不同步的副本)。
    struct Param
    {
        std::string id;      // 稳定 key,如 "ai.maxRounds"
        std::string label;   // 面板展示用中文短标签
        std::string group;   // 面板分组,如 "AI"/"网络"/"界面"
        ParamKind kind;
        std::atomic<double> value;   // 当前值(bool 用 0.0/1.0 存);跨线程读写见上
        double defVal;
        double minVal;
        double maxVal;

        Param(const std::string& id_, const std::string& label_, const std::string& group_,
              ParamKind kind_, double value_, double defVal_, double minVal_, double maxVal_)
            : id(id_), label(label_), group(group_), kind(kind_), value(value_),
              defVal(defVal_), minVal(minVal_), maxVal(maxVal_) {}
    };

    // 注册表单例:首次调用注册默认参数集(ai.maxRounds/http.retries/badge.seconds),
    // 之后每次调用返回同一个容器的引用。
    //
    // 容器选型 + 生命周期约定(务必遵守):
    // 1. 用 std::deque 而非 std::vector——deque 尾部 push_back/emplace_back 不会移动
    //    已存在的元素(只分配新的定长块并挂接,标准保证"插入或删除两端元素不会使其余
    //    元素的指针/引用失效"),而 vector 扩容会把旧元素整体搬到新内存,一旦发生
    //    realloc,find() 之前返回的所有 Param* 全部悬空。Param 里的 std::atomic<double>
    //    又不可拷贝/移动,vector<Param> 连 reserve()/emplace_back() 都编译不过(会触发
    //    reallocate 路径要求的 MoveInsertable 静态断言)——deque 天然规避了这个问题,
    //    不需要预估容量。
    // 2. 参数注册仅限启动期单线程完成(本 .cpp 内的静态初始化 lambda,只在首次调用
    //    params() 时跑一次)。运行期任何代码都不得再对本注册表做插入/删除(不得新增
    //    emplace_back/push_back,不得 erase)——find() 返回的 Param* 依赖"注册表元素
    //    个数与地址在启动后不再变化"这一前提才能在整个进程生命周期内保持有效。以后如
    //    果要支持运行期动态增删参数,必须重新设计生命周期管理(例如改用稳定分配的堆对象
    //    + 单独的增删锁),不能简单地在这个 deque 上继续 push_back。
    std::deque<Param>& params();

    // 读取当前值,按 kind 转换。找不到 id 时返回 0/0.0/false 并输出 OSG_WARN 告警。
    int getInt(const std::string& id);
    double getDouble(const std::string& id);
    bool getBool(const std::string& id);

    // 写入并按 [minVal, maxVal] 钳制;找不到 id 时静默忽略(告警)。
    void setValue(const std::string& id, double v);

    // 重置为该参数的 defVal。
    void resetDefault(const std::string& id);
}
#endif
