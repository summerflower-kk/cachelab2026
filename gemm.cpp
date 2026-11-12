#include "cachelab.h"

/*
 * 整体思路：寄存器分块（register tiling）+ 访问顺序调度
 *   - 每次在寄存器里累加 C 的一个 R×CC 小块，k 从头扫到尾后一次写回，
 *     C 的每个元素只写一次、从不读取；
 *   - 每个 k 先把较短的一边（R 与 CC 中较小者）装进寄存器，再逐个流过另一边，
 *     于是一次 k 迭代只有 R + CC 次 load，却完成 R×CC 次乘加；
 *   - 小块之间按「蛇形」顺序遍历，且相邻小块的 k 方向交替（K_UP / K_DOWN），
 *     让上一块最后用到、仍留在 cache 里的 A 行 / B 行被下一块最先用到。
 * 小块形状、遍历方向均由离线模拟搜索得到，见实验报告。
 */

/* ---------- 编译期展开：行用 R_x，列用 C_x（同名宏不能嵌套展开，故分两套） ---------- */
#define R_1(X, a) X(0, a)
#define R_2(X, a) R_1(X, a) X(1, a)
#define R_3(X, a) R_2(X, a) X(2, a)
#define R_4(X, a) R_3(X, a) X(3, a)
#define R_5(X, a) R_4(X, a) X(4, a)
#define R_6(X, a) R_5(X, a) X(5, a)
#define R_7(X, a) R_6(X, a) X(6, a)
#define R_8(X, a) R_7(X, a) X(7, a)
#define C_1(X, t) X(t, 0)
#define C_2(X, t) C_1(X, t) X(t, 1)
#define C_3(X, t) C_2(X, t) X(t, 2)
#define C_4(X, t) C_3(X, t) X(t, 3)
#define C_5(X, t) C_4(X, t) X(t, 4)
#define C_6(X, t) C_5(X, t) X(t, 5)
#define C_7(X, t) C_6(X, t) X(t, 6)
#define C_8(X, t) C_7(X, t) X(t, 7)

/* k 的两种扫描方向；n、p 是各 case 用 #define 给出的立即数 */
#define K_UP for (reg k = 0; k < n; ++k)
#define K_DOWN for (reg k = n - 1; k >= 0; --k)

/* 小块左上角为 (i, j)：A[i+t][k] 与 B[k][j+u] 的地址 */
#define A_AT(t) A[(i + t) * n + k]
#define B_AT(u) B[k * p + j + u]
#define STORE(t, u) C[(i + t) * p + j + u] = acc[t][u];
#define STORE_ROW(t, CC) C_##CC(STORE, t)

/* 形状一（R >= CC）：B 的一行片段 b[0..CC) 常驻寄存器，A 的 R 个元素逐个流过 */
#define LOAD_B(_, u) b[u] = B_AT(u);
#define FMA_B(t, u) acc[t][u] += a * b[u];
#define ROW_STEP(t, CC) a = A_AT(t); C_##CC(FMA_B, t)
#define TILE_BREG(R, CC, KLOOP) \
  {                             \
    reg acc[R][CC];             \
    reg b[CC];                  \
    reg a;                      \
    KLOOP                       \
    {                           \
      C_##CC(LOAD_B, 0)         \
      R_##R(ROW_STEP, CC)       \
    }                           \
    R_##R(STORE_ROW, CC)        \
  }

/* 形状二（R < CC）：A 的一列片段 a[0..R) 常驻寄存器，B 的 CC 个元素逐个流过 */
#define LOAD_A(t, _) a[t] = A_AT(t);
#define FMA_A(t, u) acc[t][u] += a[t] * b;
#define COL_STEP(R, u) b = B_AT(u); R_##R(FMA_A, u)
#define TILE_AREG(R, CC, KLOOP) \
  {                             \
    reg acc[R][CC];             \
    reg a[R];                   \
    reg b;                      \
    KLOOP                       \
    {                           \
      R_##R(LOAD_A, 0)          \
      C_##CC(COL_STEP, R)       \
    }                           \
    R_##R(STORE_ROW, CC)        \
  }

/* ================================ case0 ================================ */
#define m case0_m
#define n case0_n
#define p case0_p
void gemm_case0(ptr_reg A, ptr_reg B, ptr_reg C, ptr_reg buffer)
{
  reg i = 0;
  reg j = 0;
  TILE_BREG(2, 2, K_UP)
}
#undef m
#undef n
#undef p

/* ================================ case1 ================================ */
#define m case1_m
#define n case1_n
#define p case1_p
void gemm_case1(ptr_reg A, ptr_reg B, ptr_reg C, ptr_reg buffer)
{
  /* 行分块 4 + 6 + 6，列分块 4×4；块行之间蛇形，块与块之间 k 方向交替 */
  reg i = 0;
  for (reg j = 0; j < p; j += 4)
  {
    TILE_BREG(4, 4, K_UP)
    j += 4;
    TILE_BREG(4, 4, K_DOWN)
  }
  i = 4;
  for (reg j = p - 4; j >= 0; j -= 4)
  {
    TILE_BREG(6, 4, K_UP)
    j -= 4;
    TILE_BREG(6, 4, K_DOWN)
  }
  i = 10;
  for (reg j = 0; j < p; j += 4)
  {
    TILE_BREG(6, 4, K_UP)
    j += 4;
    TILE_BREG(6, 4, K_DOWN)
  }
}
#undef m
#undef n
#undef p

/* ================================ case2 ================================ */
#define m case2_m
#define n case2_n
#define p case2_p
void gemm_case2(ptr_reg A, ptr_reg B, ptr_reg C, ptr_reg buffer)
{
  /* 4×4 小块，逐块行从左到右；块与块之间 k 方向交替 */
  for (reg i = 0; i < m; i += 4)
    for (reg j = 0; j < p; j += 4)
    {
      TILE_AREG(4, 4, K_UP)
      j += 4;
      TILE_AREG(4, 4, K_DOWN)
    }
}
#undef m
#undef n
#undef p

/* ================================ case3 ================================ */
#define m case3_m
#define n case3_n
#define p case3_p
void gemm_case3(ptr_reg A, ptr_reg B, ptr_reg C, ptr_reg buffer)
{
  /* 列分块 3 + 6×4、行分块 4×6 + 3；列块在外层，列块内块行蛇形，k 方向逐块交替 */
  reg j = 0;
  for (reg i = 0; i < 24; i += 4) /* 第一个列块（宽 3），自上而下 */
  {
    TILE_BREG(4, 3, K_UP)
    i += 4;
    TILE_BREG(4, 3, K_DOWN)
  }
  {
    reg i = 24;
    TILE_BREG(3, 3, K_UP)
  }
  for (j = 3; j < p; j += 6)
  {
    { /* 奇数号列块：自下而上 */
      reg i = 24;
      TILE_AREG(3, 6, K_DOWN)
    }
    for (reg i = 20; i >= 0; i -= 4)
    {
      TILE_AREG(4, 6, K_UP)
      i -= 4;
      TILE_AREG(4, 6, K_DOWN)
    }
    j += 6;
    for (reg i = 0; i < 24; i += 4) /* 偶数号列块：自上而下 */
    {
      TILE_AREG(4, 6, K_UP)
      i += 4;
      TILE_AREG(4, 6, K_DOWN)
    }
    {
      reg i = 24;
      TILE_AREG(3, 6, K_UP)
    }
  }
}
#undef m
#undef n
#undef p
