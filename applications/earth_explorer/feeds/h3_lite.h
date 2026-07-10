#ifndef EARTH_FEEDS_H3_LITE_H
#define EARTH_FEEDS_H3_LITE_H

// h3_lite.h — 最小 H3 解码:仅 cellToLatLng(H3 cell id → 六边形中心经纬度)。
// 移植自 Uber H3 v4.1.0(https://github.com/uber/h3,Apache License 2.0),
// 提取自 h3Index.c / faceijk.c / coordijk.c / latLng.c 的解码路径;全部表格
// (baseCellData 122 条、faceNeighbors、faceCenterGeo、faceAxesAzRadsCII 等)
// 由脚本从官方源码逐字生成,零手抄。用途:GPSJam 源(gpsjam_feed.cpp)的
// 每日 CSV 只给 H3 res4 cell id、不给坐标,必须本地解码才能落点上球。
// 正确性:以 python h3(官方绑定)为 ground truth,对真实数据全量 4.7 万 cell
// + 全部 12 个 res-4 五边形对拍,中心点偏差 < 1e-11 度;仓内单测含五边形用例
// (见 tests/feed_layer_tests.cpp)。
// 只做解码不做编码/邻居/边界——需要更多能力时应引入完整 h3 库而非扩展本文件。

#include <stdint.h>
#include <math.h>

namespace h3lite
{
    struct LatLng { double lat, lng; };             // 弧度
    struct CoordIJK { int i, j, k; };
    struct FaceIJK { int face; CoordIJK coord; };
    struct FaceOrientIJK { int face; CoordIJK translate; int ccwRot60; };
    struct BaseCellData { FaceIJK homeFijk; int isPentagon; int cwOffsetPent[2]; };

    // ---- 常量(constants.h)----
    static const double kEpsilon = 0.0000000000000001;
    static const double kSqrt7 = 2.6457513110645905905016157536392604257102;
    static const double kSqrt3_2 = 0.8660254037844386467637231707529361834714;
    static const double kAp7RotRads = 0.333473172251832115336090755351601070065900389;
    static const double kRes0UGnomonic = 0.38196601125010500003;
    static const double kPi = 3.14159265358979323846;
    static const double k2Pi = 6.28318530717958647692528676655900576839433;
    static const double kPi_2 = 1.5707963267948966;
    // 方位(Direction):0=CENTER 1=K 2=J 3=JK 4=I 5=IK 6=IJ 7=INVALID
    enum { JK_QUAD = 3, KI_QUAD = 2, IJ_QUAD = 1 };   // faceNeighbors 象限下标

    // ---- 表格(官方源码逐字提取)----
static const LatLng faceCenterGeo[20] = {
    {0.803582649718989942, 1.248397419617396099},    // face  0
    {1.307747883455638156, 2.536945009877921159},    // face  1
    {1.054751253523952054, -1.347517358900396623},   // face  2
    {0.600191595538186799, -0.450603909469755746},   // face  3
    {0.491715428198773866, 0.401988202911306943},    // face  4
    {0.172745327415618701, 1.678146885280433686},    // face  5
    {0.605929321571350690, 2.953923329812411617},    // face  6
    {0.427370518328979641, -1.888876200336285401},   // face  7
    {-0.079066118549212831, -0.733429513380867741},  // face  8
    {-0.230961644455383637, 0.506495587332349035},   // face  9
    {0.079066118549212831, 2.408163140208925497},    // face 10
    {0.230961644455383637, -2.635097066257444203},   // face 11
    {-0.172745327415618701, -1.463445768309359553},  // face 12
    {-0.605929321571350690, -0.187669323777381622},  // face 13
    {-0.427370518328979641, 1.252716453253507838},   // face 14
    {-0.600191595538186799, 2.690988744120037492},   // face 15
    {-0.491715428198773866, -2.739604450678486295},  // face 16
    {-0.803582649718989942, -1.893195233972397139},  // face 17
    {-1.307747883455638156, -0.604647643711872080},  // face 18
    {-1.054751253523952054, 1.794075294689396615},   // face 19
};

static const double faceAxesAzRadsCII[20][3] = {
    {5.619958268523939882, 3.525563166130744542,
     1.431168063737548730},  // face  0
    {5.760339081714187279, 3.665943979320991689,
     1.571548876927796127},  // face  1
    {0.780213654393430055, 4.969003859179821079,
     2.874608756786625655},  // face  2
    {0.430469363979999913, 4.619259568766391033,
     2.524864466373195467},  // face  3
    {6.130269123335111400, 4.035874020941915804,
     1.941478918548720291},  // face  4
    {2.692877706530642877, 0.598482604137447119,
     4.787272808923838195},  // face  5
    {2.982963003477243874, 0.888567901084048369,
     5.077358105870439581},  // face  6
    {3.532912002790141181, 1.438516900396945656,
     5.627307105183336758},  // face  7
    {3.494305004259568154, 1.399909901866372864,
     5.588700106652763840},  // face  8
    {3.003214169499538391, 0.908819067106342928,
     5.097609271892733906},  // face  9
    {5.930472956509811562, 3.836077854116615875,
     1.741682751723420374},  // face 10
    {0.138378484090254847, 4.327168688876645809,
     2.232773586483450311},  // face 11
    {0.448714947059150361, 4.637505151845541521,
     2.543110049452346120},  // face 12
    {0.158629650112549365, 4.347419854898940135,
     2.253024752505744869},  // face 13
    {5.891865957979238535, 3.797470855586042958,
     1.703075753192847583},  // face 14
    {2.711123289609793325, 0.616728187216597771,
     4.805518392002988683},  // face 15
    {3.294508837434268316, 1.200113735041072948,
     5.388903939827463911},  // face 16
    {3.804819692245439833, 1.710424589852244509,
     5.899214794638635174},  // face 17
    {3.664438879055192436, 1.570043776661997111,
     5.758833981448388027},  // face 18
    {2.361378999196363184, 0.266983896803167583,
     4.455774101589558636},  // face 19
};

static const FaceOrientIJK faceNeighbors[20][4] = {
    {
        // face 0
        {0, {0, 0, 0}, 0},  // central face
        {4, {2, 0, 2}, 1},  // ij quadrant
        {1, {2, 2, 0}, 5},  // ki quadrant
        {5, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 1
        {1, {0, 0, 0}, 0},  // central face
        {0, {2, 0, 2}, 1},  // ij quadrant
        {2, {2, 2, 0}, 5},  // ki quadrant
        {6, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 2
        {2, {0, 0, 0}, 0},  // central face
        {1, {2, 0, 2}, 1},  // ij quadrant
        {3, {2, 2, 0}, 5},  // ki quadrant
        {7, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 3
        {3, {0, 0, 0}, 0},  // central face
        {2, {2, 0, 2}, 1},  // ij quadrant
        {4, {2, 2, 0}, 5},  // ki quadrant
        {8, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 4
        {4, {0, 0, 0}, 0},  // central face
        {3, {2, 0, 2}, 1},  // ij quadrant
        {0, {2, 2, 0}, 5},  // ki quadrant
        {9, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 5
        {5, {0, 0, 0}, 0},   // central face
        {10, {2, 2, 0}, 3},  // ij quadrant
        {14, {2, 0, 2}, 3},  // ki quadrant
        {0, {0, 2, 2}, 3}    // jk quadrant
    },
    {
        // face 6
        {6, {0, 0, 0}, 0},   // central face
        {11, {2, 2, 0}, 3},  // ij quadrant
        {10, {2, 0, 2}, 3},  // ki quadrant
        {1, {0, 2, 2}, 3}    // jk quadrant
    },
    {
        // face 7
        {7, {0, 0, 0}, 0},   // central face
        {12, {2, 2, 0}, 3},  // ij quadrant
        {11, {2, 0, 2}, 3},  // ki quadrant
        {2, {0, 2, 2}, 3}    // jk quadrant
    },
    {
        // face 8
        {8, {0, 0, 0}, 0},   // central face
        {13, {2, 2, 0}, 3},  // ij quadrant
        {12, {2, 0, 2}, 3},  // ki quadrant
        {3, {0, 2, 2}, 3}    // jk quadrant
    },
    {
        // face 9
        {9, {0, 0, 0}, 0},   // central face
        {14, {2, 2, 0}, 3},  // ij quadrant
        {13, {2, 0, 2}, 3},  // ki quadrant
        {4, {0, 2, 2}, 3}    // jk quadrant
    },
    {
        // face 10
        {10, {0, 0, 0}, 0},  // central face
        {5, {2, 2, 0}, 3},   // ij quadrant
        {6, {2, 0, 2}, 3},   // ki quadrant
        {15, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 11
        {11, {0, 0, 0}, 0},  // central face
        {6, {2, 2, 0}, 3},   // ij quadrant
        {7, {2, 0, 2}, 3},   // ki quadrant
        {16, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 12
        {12, {0, 0, 0}, 0},  // central face
        {7, {2, 2, 0}, 3},   // ij quadrant
        {8, {2, 0, 2}, 3},   // ki quadrant
        {17, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 13
        {13, {0, 0, 0}, 0},  // central face
        {8, {2, 2, 0}, 3},   // ij quadrant
        {9, {2, 0, 2}, 3},   // ki quadrant
        {18, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 14
        {14, {0, 0, 0}, 0},  // central face
        {9, {2, 2, 0}, 3},   // ij quadrant
        {5, {2, 0, 2}, 3},   // ki quadrant
        {19, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 15
        {15, {0, 0, 0}, 0},  // central face
        {16, {2, 0, 2}, 1},  // ij quadrant
        {19, {2, 2, 0}, 5},  // ki quadrant
        {10, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 16
        {16, {0, 0, 0}, 0},  // central face
        {17, {2, 0, 2}, 1},  // ij quadrant
        {15, {2, 2, 0}, 5},  // ki quadrant
        {11, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 17
        {17, {0, 0, 0}, 0},  // central face
        {18, {2, 0, 2}, 1},  // ij quadrant
        {16, {2, 2, 0}, 5},  // ki quadrant
        {12, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 18
        {18, {0, 0, 0}, 0},  // central face
        {19, {2, 0, 2}, 1},  // ij quadrant
        {17, {2, 2, 0}, 5},  // ki quadrant
        {13, {0, 2, 2}, 3}   // jk quadrant
    },
    {
        // face 19
        {19, {0, 0, 0}, 0},  // central face
        {15, {2, 0, 2}, 1},  // ij quadrant
        {18, {2, 2, 0}, 5},  // ki quadrant
        {14, {0, 2, 2}, 3}   // jk quadrant
    }};

static const int maxDimByCIIres[] = {
    2,        // res  0
    -1,       // res  1
    14,       // res  2
    -1,       // res  3
    98,       // res  4
    -1,       // res  5
    686,      // res  6
    -1,       // res  7
    4802,     // res  8
    -1,       // res  9
    33614,    // res 10
    -1,       // res 11
    235298,   // res 12
    -1,       // res 13
    1647086,  // res 14
    -1,       // res 15
    11529602  // res 16
};

static const int unitScaleByCIIres[] = {
    1,       // res  0
    -1,      // res  1
    7,       // res  2
    -1,      // res  3
    49,      // res  4
    -1,      // res  5
    343,     // res  6
    -1,      // res  7
    2401,    // res  8
    -1,      // res  9
    16807,   // res 10
    -1,      // res 11
    117649,  // res 12
    -1,      // res 13
    823543,  // res 14
    -1,      // res 15
    5764801  // res 16
};

static const CoordIJK UNIT_VECS[] = {
    {0, 0, 0},  // direction 0
    {0, 0, 1},  // direction 1
    {0, 1, 0},  // direction 2
    {0, 1, 1},  // direction 3
    {1, 0, 0},  // direction 4
    {1, 0, 1},  // direction 5
    {1, 1, 0}   // direction 6
};

static const BaseCellData baseCellData[122] = {

    {{1, {1, 0, 0}}, 0, {0, 0}},     // base cell 0
    {{2, {1, 1, 0}}, 0, {0, 0}},     // base cell 1
    {{1, {0, 0, 0}}, 0, {0, 0}},     // base cell 2
    {{2, {1, 0, 0}}, 0, {0, 0}},     // base cell 3
    {{0, {2, 0, 0}}, 1, {-1, -1}},   // base cell 4
    {{1, {1, 1, 0}}, 0, {0, 0}},     // base cell 5
    {{1, {0, 0, 1}}, 0, {0, 0}},     // base cell 6
    {{2, {0, 0, 0}}, 0, {0, 0}},     // base cell 7
    {{0, {1, 0, 0}}, 0, {0, 0}},     // base cell 8
    {{2, {0, 1, 0}}, 0, {0, 0}},     // base cell 9
    {{1, {0, 1, 0}}, 0, {0, 0}},     // base cell 10
    {{1, {0, 1, 1}}, 0, {0, 0}},     // base cell 11
    {{3, {1, 0, 0}}, 0, {0, 0}},     // base cell 12
    {{3, {1, 1, 0}}, 0, {0, 0}},     // base cell 13
    {{11, {2, 0, 0}}, 1, {2, 6}},    // base cell 14
    {{4, {1, 0, 0}}, 0, {0, 0}},     // base cell 15
    {{0, {0, 0, 0}}, 0, {0, 0}},     // base cell 16
    {{6, {0, 1, 0}}, 0, {0, 0}},     // base cell 17
    {{0, {0, 0, 1}}, 0, {0, 0}},     // base cell 18
    {{2, {0, 1, 1}}, 0, {0, 0}},     // base cell 19
    {{7, {0, 0, 1}}, 0, {0, 0}},     // base cell 20
    {{2, {0, 0, 1}}, 0, {0, 0}},     // base cell 21
    {{0, {1, 1, 0}}, 0, {0, 0}},     // base cell 22
    {{6, {0, 0, 1}}, 0, {0, 0}},     // base cell 23
    {{10, {2, 0, 0}}, 1, {1, 5}},    // base cell 24
    {{6, {0, 0, 0}}, 0, {0, 0}},     // base cell 25
    {{3, {0, 0, 0}}, 0, {0, 0}},     // base cell 26
    {{11, {1, 0, 0}}, 0, {0, 0}},    // base cell 27
    {{4, {1, 1, 0}}, 0, {0, 0}},     // base cell 28
    {{3, {0, 1, 0}}, 0, {0, 0}},     // base cell 29
    {{0, {0, 1, 1}}, 0, {0, 0}},     // base cell 30
    {{4, {0, 0, 0}}, 0, {0, 0}},     // base cell 31
    {{5, {0, 1, 0}}, 0, {0, 0}},     // base cell 32
    {{0, {0, 1, 0}}, 0, {0, 0}},     // base cell 33
    {{7, {0, 1, 0}}, 0, {0, 0}},     // base cell 34
    {{11, {1, 1, 0}}, 0, {0, 0}},    // base cell 35
    {{7, {0, 0, 0}}, 0, {0, 0}},     // base cell 36
    {{10, {1, 0, 0}}, 0, {0, 0}},    // base cell 37
    {{12, {2, 0, 0}}, 1, {3, 7}},    // base cell 38
    {{6, {1, 0, 1}}, 0, {0, 0}},     // base cell 39
    {{7, {1, 0, 1}}, 0, {0, 0}},     // base cell 40
    {{4, {0, 0, 1}}, 0, {0, 0}},     // base cell 41
    {{3, {0, 0, 1}}, 0, {0, 0}},     // base cell 42
    {{3, {0, 1, 1}}, 0, {0, 0}},     // base cell 43
    {{4, {0, 1, 0}}, 0, {0, 0}},     // base cell 44
    {{6, {1, 0, 0}}, 0, {0, 0}},     // base cell 45
    {{11, {0, 0, 0}}, 0, {0, 0}},    // base cell 46
    {{8, {0, 0, 1}}, 0, {0, 0}},     // base cell 47
    {{5, {0, 0, 1}}, 0, {0, 0}},     // base cell 48
    {{14, {2, 0, 0}}, 1, {0, 9}},    // base cell 49
    {{5, {0, 0, 0}}, 0, {0, 0}},     // base cell 50
    {{12, {1, 0, 0}}, 0, {0, 0}},    // base cell 51
    {{10, {1, 1, 0}}, 0, {0, 0}},    // base cell 52
    {{4, {0, 1, 1}}, 0, {0, 0}},     // base cell 53
    {{12, {1, 1, 0}}, 0, {0, 0}},    // base cell 54
    {{7, {1, 0, 0}}, 0, {0, 0}},     // base cell 55
    {{11, {0, 1, 0}}, 0, {0, 0}},    // base cell 56
    {{10, {0, 0, 0}}, 0, {0, 0}},    // base cell 57
    {{13, {2, 0, 0}}, 1, {4, 8}},    // base cell 58
    {{10, {0, 0, 1}}, 0, {0, 0}},    // base cell 59
    {{11, {0, 0, 1}}, 0, {0, 0}},    // base cell 60
    {{9, {0, 1, 0}}, 0, {0, 0}},     // base cell 61
    {{8, {0, 1, 0}}, 0, {0, 0}},     // base cell 62
    {{6, {2, 0, 0}}, 1, {11, 15}},   // base cell 63
    {{8, {0, 0, 0}}, 0, {0, 0}},     // base cell 64
    {{9, {0, 0, 1}}, 0, {0, 0}},     // base cell 65
    {{14, {1, 0, 0}}, 0, {0, 0}},    // base cell 66
    {{5, {1, 0, 1}}, 0, {0, 0}},     // base cell 67
    {{16, {0, 1, 1}}, 0, {0, 0}},    // base cell 68
    {{8, {1, 0, 1}}, 0, {0, 0}},     // base cell 69
    {{5, {1, 0, 0}}, 0, {0, 0}},     // base cell 70
    {{12, {0, 0, 0}}, 0, {0, 0}},    // base cell 71
    {{7, {2, 0, 0}}, 1, {12, 16}},   // base cell 72
    {{12, {0, 1, 0}}, 0, {0, 0}},    // base cell 73
    {{10, {0, 1, 0}}, 0, {0, 0}},    // base cell 74
    {{9, {0, 0, 0}}, 0, {0, 0}},     // base cell 75
    {{13, {1, 0, 0}}, 0, {0, 0}},    // base cell 76
    {{16, {0, 0, 1}}, 0, {0, 0}},    // base cell 77
    {{15, {0, 1, 1}}, 0, {0, 0}},    // base cell 78
    {{15, {0, 1, 0}}, 0, {0, 0}},    // base cell 79
    {{16, {0, 1, 0}}, 0, {0, 0}},    // base cell 80
    {{14, {1, 1, 0}}, 0, {0, 0}},    // base cell 81
    {{13, {1, 1, 0}}, 0, {0, 0}},    // base cell 82
    {{5, {2, 0, 0}}, 1, {10, 19}},   // base cell 83
    {{8, {1, 0, 0}}, 0, {0, 0}},     // base cell 84
    {{14, {0, 0, 0}}, 0, {0, 0}},    // base cell 85
    {{9, {1, 0, 1}}, 0, {0, 0}},     // base cell 86
    {{14, {0, 0, 1}}, 0, {0, 0}},    // base cell 87
    {{17, {0, 0, 1}}, 0, {0, 0}},    // base cell 88
    {{12, {0, 0, 1}}, 0, {0, 0}},    // base cell 89
    {{16, {0, 0, 0}}, 0, {0, 0}},    // base cell 90
    {{17, {0, 1, 1}}, 0, {0, 0}},    // base cell 91
    {{15, {0, 0, 1}}, 0, {0, 0}},    // base cell 92
    {{16, {1, 0, 1}}, 0, {0, 0}},    // base cell 93
    {{9, {1, 0, 0}}, 0, {0, 0}},     // base cell 94
    {{15, {0, 0, 0}}, 0, {0, 0}},    // base cell 95
    {{13, {0, 0, 0}}, 0, {0, 0}},    // base cell 96
    {{8, {2, 0, 0}}, 1, {13, 17}},   // base cell 97
    {{13, {0, 1, 0}}, 0, {0, 0}},    // base cell 98
    {{17, {1, 0, 1}}, 0, {0, 0}},    // base cell 99
    {{19, {0, 1, 0}}, 0, {0, 0}},    // base cell 100
    {{14, {0, 1, 0}}, 0, {0, 0}},    // base cell 101
    {{19, {0, 1, 1}}, 0, {0, 0}},    // base cell 102
    {{17, {0, 1, 0}}, 0, {0, 0}},    // base cell 103
    {{13, {0, 0, 1}}, 0, {0, 0}},    // base cell 104
    {{17, {0, 0, 0}}, 0, {0, 0}},    // base cell 105
    {{16, {1, 0, 0}}, 0, {0, 0}},    // base cell 106
    {{9, {2, 0, 0}}, 1, {14, 18}},   // base cell 107
    {{15, {1, 0, 1}}, 0, {0, 0}},    // base cell 108
    {{15, {1, 0, 0}}, 0, {0, 0}},    // base cell 109
    {{18, {0, 1, 1}}, 0, {0, 0}},    // base cell 110
    {{18, {0, 0, 1}}, 0, {0, 0}},    // base cell 111
    {{19, {0, 0, 1}}, 0, {0, 0}},    // base cell 112
    {{17, {1, 0, 0}}, 0, {0, 0}},    // base cell 113
    {{19, {0, 0, 0}}, 0, {0, 0}},    // base cell 114
    {{18, {0, 1, 0}}, 0, {0, 0}},    // base cell 115
    {{18, {1, 0, 1}}, 0, {0, 0}},    // base cell 116
    {{19, {2, 0, 0}}, 1, {-1, -1}},  // base cell 117
    {{19, {1, 0, 0}}, 0, {0, 0}},    // base cell 118
    {{18, {0, 0, 0}}, 0, {0, 0}},    // base cell 119
    {{19, {1, 0, 1}}, 0, {0, 0}},    // base cell 120
    {{18, {1, 0, 0}}, 0, {0, 0}}     // base cell 121
};

    // ---- H3Index 位域宏(h3Index.h)----
    inline int getResolution(uint64_t h) { return (int)((h >> 52) & 15); }
    inline int getBaseCell(uint64_t h) { return (int)((h >> 45) & 127); }
    inline int getIndexDigit(uint64_t h, int res)
    { return (int)((h >> ((15 - res) * 3)) & 7); }
    inline void setIndexDigit(uint64_t& h, int res, int digit)
    {
        h = (h & ~(((uint64_t)7) << ((15 - res) * 3)))
          | (((uint64_t)digit) << ((15 - res) * 3));
    }
    inline bool isResClassIII(int res) { return (res % 2) != 0; }
    inline bool isBaseCellPentagon(int bc)
    { return (bc >= 0 && bc < 122) ? baseCellData[bc].isPentagon != 0 : false; }

    // ---- coordijk.c ----
    inline void ijkAdd(const CoordIJK& a, const CoordIJK& b, CoordIJK& s)
    { s.i = a.i + b.i; s.j = a.j + b.j; s.k = a.k + b.k; }
    inline void ijkSub(const CoordIJK& a, const CoordIJK& b, CoordIJK& d)
    { d.i = a.i - b.i; d.j = a.j - b.j; d.k = a.k - b.k; }
    inline void ijkScale(CoordIJK& c, int f) { c.i *= f; c.j *= f; c.k *= f; }
    inline void ijkNormalize(CoordIJK& c)
    {
        if (c.i < 0) { c.j -= c.i; c.k -= c.i; c.i = 0; }
        if (c.j < 0) { c.i -= c.j; c.k -= c.j; c.j = 0; }
        if (c.k < 0) { c.i -= c.k; c.j -= c.k; c.k = 0; }
        int m = c.i; if (c.j < m) m = c.j; if (c.k < m) m = c.k;
        if (m > 0) { c.i -= m; c.j -= m; c.k -= m; }
    }
    inline void neighbor(CoordIJK& ijk, int digit)
    { if (digit > 0 && digit < 7) { ijkAdd(ijk, UNIT_VECS[digit], ijk); ijkNormalize(ijk); } }
    inline void ijkRotate60ccw(CoordIJK& ijk)
    {
        CoordIJK iv = {1, 1, 0}, jv = {0, 1, 1}, kv = {1, 0, 1};
        ijkScale(iv, ijk.i); ijkScale(jv, ijk.j); ijkScale(kv, ijk.k);
        ijkAdd(iv, jv, ijk); ijkAdd(ijk, kv, ijk); ijkNormalize(ijk);
    }
    inline void ijkRotate60cw(CoordIJK& ijk)
    {
        CoordIJK iv = {1, 0, 1}, jv = {1, 1, 0}, kv = {0, 1, 1};
        ijkScale(iv, ijk.i); ijkScale(jv, ijk.j); ijkScale(kv, ijk.k);
        ijkAdd(iv, jv, ijk); ijkAdd(ijk, kv, ijk); ijkNormalize(ijk);
    }
    inline void downAp7(CoordIJK& ijk)
    {
        CoordIJK iv = {3, 0, 1}, jv = {1, 3, 0}, kv = {0, 1, 3};
        ijkScale(iv, ijk.i); ijkScale(jv, ijk.j); ijkScale(kv, ijk.k);
        ijkAdd(iv, jv, ijk); ijkAdd(ijk, kv, ijk); ijkNormalize(ijk);
    }
    inline void downAp7r(CoordIJK& ijk)
    {
        CoordIJK iv = {3, 1, 0}, jv = {0, 3, 1}, kv = {1, 0, 3};
        ijkScale(iv, ijk.i); ijkScale(jv, ijk.j); ijkScale(kv, ijk.k);
        ijkAdd(iv, jv, ijk); ijkAdd(ijk, kv, ijk); ijkNormalize(ijk);
    }
    inline void upAp7r(CoordIJK& ijk)
    {
        int i = ijk.i - ijk.k, j = ijk.j - ijk.k;
        ijk.i = (int)lround((2.0 * i + j) / 7.0);
        ijk.j = (int)lround((3.0 * j - i) / 7.0);
        ijk.k = 0; ijkNormalize(ijk);
    }
    // 数位顺时针旋 60°(K1→JK3→J2→IJ6→I4→IK5→K1;0/7 不变)
    inline int rotate60cw(int digit)
    {
        switch (digit)
        {
            case 1: return 3; case 3: return 2; case 2: return 6;
            case 6: return 4; case 4: return 5; case 5: return 1;
            default: return digit;
        }
    }

    // ---- h3Index.c ----
    inline int leadingNonZeroDigit(uint64_t h)
    {
        for (int r = 1; r <= getResolution(h); r++)
        { if (getIndexDigit(h, r)) return getIndexDigit(h, r); }
        return 0;
    }
    inline uint64_t h3Rotate60cw(uint64_t h)
    {
        for (int r = 1, res = getResolution(h); r <= res; r++)
            setIndexDigit(h, r, rotate60cw(getIndexDigit(h, r)));
        return h;
    }
    inline int h3ToFaceIjkWithInitializedFijk(uint64_t h, FaceIJK& fijk)
    {
        CoordIJK& ijk = fijk.coord;
        int res = getResolution(h);
        int possibleOverage = 1;
        if (!isBaseCellPentagon(getBaseCell(h)) &&
            (res == 0 || (ijk.i == 0 && ijk.j == 0 && ijk.k == 0)))
            possibleOverage = 0;
        for (int r = 1; r <= res; r++)
        {
            if (isResClassIII(r)) downAp7(ijk);
            else downAp7r(ijk);
            neighbor(ijk, getIndexDigit(h, r));
        }
        return possibleOverage;
    }

    // ---- faceijk.c:跨面溢出修正 ----
    enum Overage { NO_OVERAGE = 0, FACE_EDGE = 1, NEW_FACE = 2 };
    inline Overage adjustOverageClassII(FaceIJK& fijk, int res, int pentLeading4, int substrate)
    {
        Overage overage = NO_OVERAGE;
        CoordIJK& ijk = fijk.coord;
        int maxDim = maxDimByCIIres[res];
        if (substrate) maxDim *= 3;
        if (substrate && ijk.i + ijk.j + ijk.k == maxDim) overage = FACE_EDGE;
        else if (ijk.i + ijk.j + ijk.k > maxDim)
        {
            overage = NEW_FACE;
            const FaceOrientIJK* fo;
            if (ijk.k > 0)
            {
                if (ijk.j > 0) fo = &faceNeighbors[fijk.face][JK_QUAD];
                else
                {
                    fo = &faceNeighbors[fijk.face][KI_QUAD];
                    if (pentLeading4)
                    {
                        // 五边形缺失子序列:平移原点→旋转→平移回
                        CoordIJK origin = {maxDim, 0, 0}, tmp;
                        ijkSub(ijk, origin, tmp);
                        ijkRotate60cw(tmp);
                        ijkAdd(tmp, origin, ijk);
                    }
                }
            }
            else fo = &faceNeighbors[fijk.face][IJ_QUAD];
            fijk.face = fo->face;
            for (int i = 0; i < fo->ccwRot60; i++) ijkRotate60ccw(ijk);
            CoordIJK trans = fo->translate;
            int unitScale = unitScaleByCIIres[res];
            if (substrate) unitScale *= 3;
            ijkScale(trans, unitScale);
            ijkAdd(ijk, trans, ijk);
            ijkNormalize(ijk);
            if (substrate && ijk.i + ijk.j + ijk.k == maxDim) overage = FACE_EDGE;
        }
        return overage;
    }

    // ---- h3Index.c:H3 → 面坐标(返回 false = 非法 base cell)----
    inline bool h3ToFaceIjk(uint64_t h, FaceIJK& fijk)
    {
        int baseCell = getBaseCell(h);
        if (baseCell < 0 || baseCell >= 122) return false;
        if (isBaseCellPentagon(baseCell) && leadingNonZeroDigit(h) == 5)
            h = h3Rotate60cw(h);
        fijk = baseCellData[baseCell].homeFijk;
        if (!h3ToFaceIjkWithInitializedFijk(h, fijk)) return true;   // 无跨面可能
        CoordIJK origIJK = fijk.coord;
        int res = getResolution(h);
        if (isResClassIII(res)) { downAp7r(fijk.coord); res++; }
        int pentLeading4 = (isBaseCellPentagon(baseCell) && leadingNonZeroDigit(h) == 4);
        if (adjustOverageClassII(fijk, res, pentLeading4, 0) != NO_OVERAGE)
        {
            if (isBaseCellPentagon(baseCell))
            { while (adjustOverageClassII(fijk, res, 0, 0) != NO_OVERAGE) {} }
            if (res != getResolution(h)) upAp7r(fijk.coord);
        }
        else if (res != getResolution(h)) fijk.coord = origIJK;
        return true;
    }

    // ---- latLng.c / faceijk.c:面坐标 → 球面经纬度 ----
    inline double posAngleRads(double rads)
    {
        double tmp = (rads < 0.0) ? rads + k2Pi : rads;
        if (rads >= k2Pi) tmp -= k2Pi;
        return tmp;
    }
    inline double constrainLng(double lng)
    {
        while (lng > kPi) lng -= 2.0 * kPi;
        while (lng < -kPi) lng += 2.0 * kPi;
        return lng;
    }
    inline void geoAzDistanceRads(const LatLng& p1, double az, double distance, LatLng& p2)
    {
        if (distance < kEpsilon) { p2 = p1; return; }
        az = posAngleRads(az);
        if (az < kEpsilon || fabs(az - kPi) < kEpsilon)   // 正北/正南
        {
            if (az < kEpsilon) p2.lat = p1.lat + distance;
            else p2.lat = p1.lat - distance;
            if (fabs(p2.lat - kPi_2) < kEpsilon) { p2.lat = kPi_2; p2.lng = 0.0; }
            else if (fabs(p2.lat + kPi_2) < kEpsilon) { p2.lat = -kPi_2; p2.lng = 0.0; }
            else p2.lng = constrainLng(p1.lng);
        }
        else
        {
            double sinlat = sin(p1.lat) * cos(distance) + cos(p1.lat) * sin(distance) * cos(az);
            if (sinlat > 1.0) sinlat = 1.0;
            if (sinlat < -1.0) sinlat = -1.0;
            p2.lat = asin(sinlat);
            if (fabs(p2.lat - kPi_2) < kEpsilon) { p2.lat = kPi_2; p2.lng = 0.0; }
            else if (fabs(p2.lat + kPi_2) < kEpsilon) { p2.lat = -kPi_2; p2.lng = 0.0; }
            else
            {
                double sinlng = sin(az) * sin(distance) / cos(p2.lat);
                double coslng = (cos(distance) - sin(p1.lat) * sin(p2.lat))
                              / cos(p1.lat) / cos(p2.lat);
                if (sinlng > 1.0) sinlng = 1.0;
                if (sinlng < -1.0) sinlng = -1.0;
                if (coslng > 1.0) coslng = 1.0;
                if (coslng < -1.0) coslng = -1.0;
                p2.lng = constrainLng(p1.lng + atan2(sinlng, coslng));
            }
        }
    }
    inline void hex2dToGeo(double x, double y, int face, int res, LatLng& g)
    {
        double r = sqrt(x * x + y * y);
        if (r < kEpsilon) { g = faceCenterGeo[face]; return; }
        double theta = atan2(y, x);
        for (int i = 0; i < res; i++) r /= kSqrt7;
        r *= kRes0UGnomonic;
        r = atan(r);   // 逆 gnomonic 投影
        if (isResClassIII(res)) theta = posAngleRads(theta + kAp7RotRads);
        theta = posAngleRads(faceAxesAzRadsCII[face][0] - theta);
        geoAzDistanceRads(faceCenterGeo[face], theta, r, g);
    }
    inline void faceIjkToGeo(const FaceIJK& h, int res, LatLng& g)
    {
        int i = h.coord.i - h.coord.k, j = h.coord.j - h.coord.k;
        double x = i - 0.5 * j, y = j * kSqrt3_2;   // _ijkToHex2d
        hex2dToGeo(x, y, h.face, res, g);
    }

    // ---- 对外唯一入口:H3 cell id → 中心点经纬度(度)。false = 非法 base cell ----
    inline bool cellToLatLngDeg(uint64_t h, double& latDeg, double& lngDeg)
    {
        FaceIJK fijk;
        if (!h3ToFaceIjk(h, fijk)) return false;
        LatLng g;
        faceIjkToGeo(fijk, getResolution(h), g);
        latDeg = g.lat * 180.0 / kPi; lngDeg = g.lng * 180.0 / kPi;
        return true;
    }
}

#endif
