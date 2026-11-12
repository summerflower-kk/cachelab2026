// student id: 2022201552
// please change the above line to your student id

#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

void printSummary(int hits, int misses, int evictions)
{
  printf("hits:%d misses:%d evictions:%d\n", hits, misses, evictions);
  FILE *output_fp = fopen(".csim_results", "w");
  assert(output_fp);
  fprintf(output_fp, "%d %d %d\n", hits, misses, evictions);
  fclose(output_fp);
}

void printHelp(const char *name)
{
  printf(
      "Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n"
      "Options:\n"
      "  -h         Print this help message.\n"
      "  -v         Optional verbose flag.\n"
      "  -s <num>   Number of set index bits.\n"
      "  -E <num>   Number of lines per set.\n"
      "  -b <num>   Number of block offset bits.\n"
      "  -t <file>  Trace file.\n\n"
      "Examples:\n"
      "  linux>  %s -s 4 -E 1 -b 4 -t traces/yi.trace\n"
      "  linux>  %s -v -s 8 -E 2 -b 4 -t traces/yi.trace\n",
      name, name, name);
}

/* 一个缓存行：只需记录有效位、标记和最近使用时间（LRU 用）。
 * dirty 位实现 Write-Back：写命中只置脏，不立即写内存；
 * 由于输出只统计 h/m/e，脏行被替换时的写回不额外计数。 */
typedef struct
{
  int valid;
  int dirty;
  unsigned long long tag;
  unsigned long long last_used;
} Line;

typedef struct
{
  int s, E, b;
  Line *lines; /* 共 2^s * E 行，第 i 组占 lines[i*E .. i*E+E-1] */
  unsigned long long clock;
  int hits, misses, evictions;
  int verbose;
} Cache;

/* 访问一次地址：命中则刷新时间戳；
 * 不命中则优先放入空行，组满时淘汰 last_used 最小的行（LRU）。
 * Write-Allocate：写不命中同样把块调入缓存。 */
static void access_cache(Cache *c, unsigned long long addr, int is_store)
{
  unsigned long long set_idx = (addr >> c->b) & ((1ULL << c->s) - 1);
  unsigned long long tag = addr >> (c->s + c->b);
  Line *set = c->lines + set_idx * c->E;
  Line *victim = NULL;
  Line *empty = NULL;

  c->clock++;
  for (int i = 0; i < c->E; i++)
  {
    Line *line = &set[i];
    if (line->valid && line->tag == tag)
    {
      c->hits++;
      line->last_used = c->clock;
      line->dirty |= is_store;
      if (c->verbose)
        printf(" hit");
      return;
    }
    if (!line->valid)
    {
      if (!empty)
        empty = line;
    }
    else if (!victim || line->last_used < victim->last_used)
    {
      victim = line;
    }
  }

  c->misses++;
  if (c->verbose)
    printf(" miss");
  if (!empty)
  {
    c->evictions++;
    if (c->verbose)
      printf(" eviction");
    empty = victim; /* 被淘汰行若为脏，此处即发生写回 */
  }
  empty->valid = 1;
  empty->dirty = is_store;
  empty->tag = tag;
  empty->last_used = c->clock;
}

int main(int argc, char *argv[])
{
  Cache cache = {0};
  const char *trace_name = NULL;
  int opt;

  cache.s = cache.E = cache.b = -1;
  while ((opt = getopt(argc, argv, "hvs:E:b:t:")) != -1)
  {
    switch (opt)
    {
    case 'h':
      printHelp(argv[0]);
      return 0;
    case 'v':
      cache.verbose = 1;
      break;
    case 's':
      cache.s = atoi(optarg);
      break;
    case 'E':
      cache.E = atoi(optarg);
      break;
    case 'b':
      cache.b = atoi(optarg);
      break;
    case 't':
      trace_name = optarg;
      break;
    default:
      printHelp(argv[0]);
      return 1;
    }
  }
  if (cache.s < 0 || cache.E <= 0 || cache.b < 0 || !trace_name)
  {
    printf("%s: Missing required command line argument\n", argv[0]);
    printHelp(argv[0]);
    return 1;
  }

  cache.lines = calloc((size_t)cache.E << cache.s, sizeof(Line));
  if (!cache.lines)
  {
    fprintf(stderr, "out of memory\n");
    return 1;
  }

  FILE *fp = fopen(trace_name, "r");
  if (!fp)
  {
    fprintf(stderr, "%s: No such file or directory\n", trace_name);
    free(cache.lines);
    return 1;
  }

  char op;
  unsigned long long addr;
  int size, reg_id;
  /* 每行形如 " L 0x30000000,4 8"；地址可带或不带 0x 前缀 */
  while (fscanf(fp, " %c %llx,%d %d", &op, &addr, &size, &reg_id) == 4)
  {
    if (op != 'L' && op != 'S' && op != 'M')
      continue;
    if (cache.verbose)
      printf("%c %llx,%d", op, addr, size);
    access_cache(&cache, addr, op == 'S');
    if (op == 'M') /* 修改 = 读 + 写，第二次必然命中 */
      access_cache(&cache, addr, 1);
    if (cache.verbose)
      printf("\n");
  }

  fclose(fp);
  free(cache.lines);
  printSummary(cache.hits, cache.misses, cache.evictions);
  return 0;
}
