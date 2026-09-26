// An LVGL allocation that fails ends the process instead of spinning (#653).
//
// `lv_timer_create()` asserts on the node it could not allocate before it
// checks it for null, so the caller's own null check never runs: what happens
// next is `LV_ASSERT_HANDLER`, and LVGL's default is `while(1);`. On the watch
// that spin holds the LVGL port lock; `abort()` is the policy both builds set
// instead, and this is the simulator half of proving it.
//
// The child exhausts the pool and asks for a timer, as `create_ui()` does on
// boot. Three outcomes and three messages: SIGABRT is the handler; SIGALRM is
// the old spin, which the child's own alarm turns into a failure rather than a
// hang; a clean exit means the assertion never fired, so the allocator or the
// configuration is not the one this test is about.

#include <csignal>
#include <cstdio>

#include <sys/wait.h>
#include <unistd.h>

#include "lvgl.h"

int main() {
  const pid_t child = fork();
  if (child < 0) {
    std::perror("fork");
    return 1;
  }
  if (child == 0) {
    (void)alarm(10);
    lv_init();
    while (lv_malloc(64) != nullptr) {
    }
    while (lv_malloc(1) != nullptr) {
    }
    (void)lv_timer_create([](lv_timer_t *) {}, 1000, nullptr);
    _exit(0);
  }

  int status = 0;
  if (waitpid(child, &status, 0) != child) {
    std::perror("waitpid");
    return 1;
  }
  if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
    std::puts("ok: an exhausted pool aborts");
    return 0;
  }
  if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) {
    std::fputs("FAIL: the assertion handler spun until the alarm\n", stderr);
  } else if (WIFSIGNALED(status)) {
    std::fprintf(stderr, "FAIL: died on signal %d, not SIGABRT\n",
                 WTERMSIG(status));
  } else {
    std::fprintf(stderr,
                 "FAIL: exited %d -- no assertion fired on an exhausted pool\n",
                 WEXITSTATUS(status));
  }
  return 1;
}
