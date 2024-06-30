#include <ctype.h>
#include <pthread.h>

#define MY_ALIGN(A, L) (((A) + (L) - 1) & ~((L) - 1))
#define ALIGN_SIZE(A) MY_ALIGN((A), sizeof(double))

#define TABLE_COUNTER_TYPE size_t
typedef unsigned char uchar;
typedef unsigned char uint8;
typedef char my_bool;
typedef unsigned long long int ulonglong;

struct Query_cache_block;
struct Query_cache_block_table;
struct Query_cache_table;
struct Query_cache_query;
struct Query_cache_result;
struct Query_cache_tls;

typedef pthread_rwlock_t native_rw_lock_t;
struct st_mysql_rwlock {
  native_rw_lock_t m_rwlock;
  struct PSI_rwlock *m_psi;
};
typedef struct st_mysql_rwlock mysql_rwlock_t;

const char query_cache_shm_name[] = "mysql_query_cache";

struct Query_cache_result {
  Query_cache_result() {} /* Remove gcc warning */
  Query_cache_block *query;

  inline uchar *data() {
    return (uchar *)(((uchar *)this) + ALIGN_SIZE(sizeof(Query_cache_result)));
  }
  /* data_continue (if not whole packet contained by this block) */
  inline Query_cache_block *parent() { return query; }
  inline void parent(Query_cache_block *p) { query = p; }

  inline uchar *data_with_vaddr_offset(const ptrdiff_t &vaddr_offset) {
    return data();  // data is already based on new vaddr (by using this)
  }
};

struct Query_cache_block_table {
  Query_cache_block_table() {} /* Remove gcc warning */

  /**
    This node holds a position in a static table list belonging
    to the associated query (base 0).
  */
  TABLE_COUNTER_TYPE n;

  /**
    Pointers to the next and previous node, linking all queries with
    a common table.
  */
  Query_cache_block_table *next, *prev;

  /**
    A pointer to the table-type block which all
    linked queries has in common.
  */
  Query_cache_table *parent;

  /**
    A method to calculate the address of the query cache block
    owning this node. The purpose of this calculation is to
    make it easier to move the query cache block without having
    to modify all the pointer addresses.
  */
  inline Query_cache_block *block();
};

struct Query_cache_block {
  Query_cache_block() {} /* Remove gcc warning */
  enum block_type {
    FREE,
    QUERY,
    RESULT,
    RES_CONT,
    RES_BEG,
    RES_INCOMPLETE,
    TABLE,
    INCOMPLETE
  };

  ulong length;  // length of all block
  ulong used;    // length of data
  /*
    Not used **pprev, **prev because really needed access to pervious block:
    *pprev to join free blocks
    *prev to access to opposite side of list in cyclic sorted list
  */
  Query_cache_block *pnext, *pprev,  // physical next/previous block
      *next, *prev;                  // logical next/previous block
  block_type type;
  TABLE_COUNTER_TYPE n_tables;  // number of tables in query

  inline my_bool is_free(void) { return type == FREE; }
  void init(ulong length);
  void destroy();
  inline uint headers_len() {
    return (ALIGN_SIZE(sizeof(Query_cache_block_table) * n_tables) +
            ALIGN_SIZE(sizeof(Query_cache_block)));
  }
  inline uchar *data(void) {
    return (uchar *)(((uchar *)this) + headers_len());
  }
  inline Query_cache_query *query() {
#ifndef NDEBUG
    if (type != QUERY) LOG(ERROR) << "incorrect block type" << std::endl;
#endif
    return (Query_cache_query *)data();
  }
  inline Query_cache_table *table();
  inline Query_cache_result *result() {
#ifndef NDEBUG
    if (type != RESULT) LOG(ERROR) << "incorrect block type" << std::endl;
#endif
    return (Query_cache_result *)data();
  }
  inline Query_cache_block_table *table(TABLE_COUNTER_TYPE n);

  inline Query_cache_block *next_with_vaddr_offset(const ptrdiff_t offset) {
    return reinterpret_cast<Query_cache_block *>(
        reinterpret_cast<uchar *>(next) + offset);
  }
  inline Query_cache_query *query_with_vaddr_offset(
      const ptrdiff_t &vaddr_offset) {
    return query();  // query is already based on new vaddr (by using this)
  }
  inline Query_cache_result *result_with_vaddr_offset(
      const ptrdiff_t &vaddr_offset) {
    return result();  // result is already based on new vaddr (by using this)
  }
  inline size_t result_data_len() {
    return used - headers_len() - ALIGN_SIZE(sizeof(Query_cache_result));
  }
};

struct Query_cache_query {
  ulonglong current_found_rows;
  mysql_rwlock_t lock;
  Query_cache_block *res;
  Query_cache_tls *wri;
  ulong len;
  uint8 tbls_type;
  unsigned int last_pkt_nr;

  Query_cache_query() {} /* Remove gcc warning */
  inline void init_n_lock();
  void unlock_n_destroy();
  inline ulonglong found_rows() { return current_found_rows; }
  inline void found_rows(ulonglong rows) { current_found_rows = rows; }
  inline Query_cache_block *result() { return res; }
  inline void result(Query_cache_block *p) { res = p; }
  inline Query_cache_tls *writer() { return wri; }
  inline void writer(Query_cache_tls *p);
  inline uint8 tables_type() { return tbls_type; }
  inline void tables_type(uint8 type) { tbls_type = type; }
  inline ulong length() { return len; }
  inline ulong add(ulong packet_len) { return (len += packet_len); }
  inline void length(ulong length_arg) { len = length_arg; }
  inline uchar *query() {
    return (((uchar *)this) + ALIGN_SIZE(sizeof(Query_cache_query)));
  }
  void lock_writing();
  void lock_reading();
  my_bool try_lock_writing();
  void unlock_writing();
  void unlock_reading();

  inline uchar *query_with_vaddr_offset(const ptrdiff_t &vaddr_offset) {
    return query();  // query is already based on new vaddr (by using this)
  }
  inline Query_cache_block *result_with_vaddr_offset(
      const ptrdiff_t &vaddr_offset) {
    return reinterpret_cast<Query_cache_block *>(
        reinterpret_cast<uchar *>(res) + vaddr_offset);
  }
};

union AlignedShmInfo {
  struct ShmInfo {
    uchar *vaddr;
    Query_cache_block *queries_blocks;
    Query_cache_block *queries_blocks_with_vaddr_offset(
        const ptrdiff_t &vaddr_offset) {
      return reinterpret_cast<Query_cache_block *>(
          reinterpret_cast<uchar *>(queries_blocks) + vaddr_offset);
    }
    friend std::ostream &operator<<(std::ostream &out, const ShmInfo &rhs) {
      out << "ShmInfo:" << std::endl;
      out << "  vaddr: " << static_cast<void *>(rhs.vaddr) << std::endl;
      out << "  queries_blocks: " << rhs.queries_blocks << std::endl;
      return out;
    }
  } shm_info;
  char padding[32];
};