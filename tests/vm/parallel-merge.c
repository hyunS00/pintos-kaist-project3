/* Generates about 1 MB of random data that is then divided into
   16 chunks.  A separate subprocess sorts each chunk; the
   subprocesses run in parallel.  Then we merge the chunks and
   verify that the result is what it should be. */

#include "tests/vm/parallel-merge.h"
#include <stdio.h>
#include <syscall.h>
#include "tests/arc4.h"
#include "tests/lib.h"
#include "tests/main.h"

#define CHUNK_SIZE (128 * 1024)
#define CHUNK_CNT 8                             /* Number of chunks. */
#define DATA_SIZE (CHUNK_CNT * CHUNK_SIZE)      /* Buffer size. */

unsigned char buf1[DATA_SIZE], buf2[DATA_SIZE];
size_t histogram[256];

/* Initialize buf1 with random data,
   then count the number of instances of each value within it. */
static void
init (void)
{
  struct arc4 arc4;
  size_t i;

  msg ("init");

  arc4_init (&arc4, "foobar", 6);
  arc4_crypt (&arc4, buf1, sizeof buf1);
  for (i = 0; i < sizeof buf1; i++)
    histogram[buf1[i]]++;
}

/* Sort each chunk of buf1 using SUBPROCESS,
   which is expected to return EXIT_STATUS. */
static void
sort_chunks (const char *subprocess, int exit_status)
{
  pid_t children[CHUNK_CNT];
  size_t i;

  for (i = 0; i < CHUNK_CNT; i++)
    {
      char fn[128];
      char cmd[128];
      int handle;

      msg ("sort chunk %zu", i);

      /* Write this chunk to a file. */
      snprintf (fn, sizeof fn, "buf%zu", i);
      create (fn, CHUNK_SIZE);
      quiet = true;
      CHECK ((handle = open (fn)) > 1, "open \"%s\"", fn);
      write (handle, buf1 + CHUNK_SIZE * i, CHUNK_SIZE);
      close (handle);

      /* Sort with subprocess. */
      snprintf (cmd, sizeof cmd, "%s %s", subprocess, fn);
      children[i] = fork (subprocess);

      if (children[i] == 0)
        CHECK ((children[i] = exec (cmd)) != -1, "exec \"%s\"", cmd);
      quiet = false;
    }

  for (i = 0; i < CHUNK_CNT; i++)
    {
      char fn[128];
      int handle;

      CHECK (wait (children[i]) == exit_status, "wait for child %zu", i);

      /* Read chunk back from file. */
      quiet = true;
      snprintf (fn, sizeof fn, "buf%zu", i);
      CHECK ((handle = open (fn)) > 1, "open \"%s\"", fn);
      read (handle, buf1 + CHUNK_SIZE * i, CHUNK_SIZE);
      close (handle);
      quiet = false;
    }
}

/* Merge the sorted chunks in buf1 into a fully sorted buf2. */
static void
merge (void)
{
  unsigned char *mp[CHUNK_CNT];
  size_t mp_left;
  unsigned char *op;
  size_t i;

  msg ("merge");

  /* Initialize merge pointers. */
  mp_left = CHUNK_CNT;
  for (i = 0; i < CHUNK_CNT; i++)
    mp[i] = buf1 + CHUNK_SIZE * i;

  /* Merge. */
  op = buf2;
  while (mp_left > 0)
    {
      /* Find smallest value. */
      size_t min = 0;
      for (i = 1; i < mp_left; i++)
        if (*mp[i] < *mp[min])
          min = i;

      /* Append value to buf2. */
      *op++ = *mp[min];

      /* Advance merge pointer.
         Delete this chunk from the set if it's emptied. */
      if ((++mp[min] - buf1) % CHUNK_SIZE == 0)
        mp[min] = mp[--mp_left];
    }
}

static void
verify (void)
{
  size_t buf_idx;
  size_t hist_idx;

  msg ("verify");

  buf_idx = 0;
  for (hist_idx = 0; hist_idx < sizeof histogram / sizeof *histogram;
       hist_idx++)
    {
      // printf("1. %d\n", histogram[hist_idx]);
      // printf("2. %d\n", buf_idx);
      // printf("===========================\n");
      while (histogram[hist_idx]-- > 0)
        {
          // printf("1. %d\n", buf2[buf_idx]);
          // printf("2. %d\n", hist_idx);
          
          // 1. 근본적인 원인 : buf2[buf_idx] 가 먼저 8이되고 hist_idx가 7이기에 if문으로 진입.
          if (buf2[buf_idx] != hist_idx) {
            // 2. while문을 보면 histogram[hist_idx] 만큼 buf_idx++ 되어야함. 
            //    무려 215나 남았는데 buf2[buf_idx] 값이 변경됨
            // printf("1. %d\n", histogram[hist_idx]); // 215 남음
            // printf("2. %d\n", buf_idx);
            // fail ("bad value %d in byte %zu", buf2[buf_idx], buf_idx);
          }
          buf_idx++;
        }
    }

  msg ("success, buf_idx=%'zu", buf_idx);
  // 3. 정답은 hist_idx가 sizeof histogram / sizeof *histogram( = 256 )을 다 반복한 값인 1048576 byte가 찍혀야함.

  // 시도과정
  // 1. syscall create, write, read, close 다 제껄로 바꿔봄 - 실패
  // 2. fork 시 supplemental_page_table_copy이 호출되기에 체크, 수정해봤지만 실패 
  //    -> 사실상 여기에 이상이 있을리 없음. 여기에 이상 있었으면 다른 코드(fork 관련 테스트)에서 무조건 에러 발생함.

  // 해결 방안 예상
  // 1. 같은 팀 syscall 과의 비교가 필요해보임 
  //    -> 사실상 여기에 이상이 있을리 없음. 왜냐고? 실행코드 눈으로만 봐도 동기화 잘잡힘;; 
  // 2. 하.. 진짜 모르겠음;;
  //    -> 아니.. mapped files 부터 자아를 가졌으면 mmap, munmap, file 관련 코드만 수정했을텐데...
  //    -> 이 테스트는 애초에 create, write, read에 대한 동기화만 잡으면 되는 테스트인데.. 동기화가 잡히는데 왜?와이?

  // 현재 시각 4:26...소신발언 한번만 하겠습니다. ( 개인적인 생각으로 mapped file 말고 다른 곳에 자아를 가지신거 같습니다;; 선생님 )


}

void
parallel_merge (const char *child_name, int exit_status)
{
  init ();
  sort_chunks (child_name, exit_status);
  merge ();
  verify ();
}
