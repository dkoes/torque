#include "license_pbs.h" /* See here for the software license */
#include "lib_net.h"
#include "test_get_hostname.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>


#include "pbs_error.h"

extern bool pbs_getaddrinfo_got_af_inet;

START_TEST(test_get_fullhostname)
  {
  char shortname[1024];
  char namebuf[1024];
  int rc;

  fail_unless(gethostname(shortname, sizeof(shortname)) == 0);
  rc = get_fullhostname(shortname, namebuf, sizeof(namebuf), NULL);
  fail_unless(rc != PBSE_NONE);
  fail_unless(pbs_getaddrinfo_got_af_inet);
  }
END_TEST

START_TEST(test_two)
  {


  }
END_TEST

Suite *get_hostname_suite(void)
  {
  Suite *s = suite_create("get_hostname_suite methods");
  TCase *tc_core = tcase_create("test_get_fullhostname");
  tcase_add_test(tc_core, test_get_fullhostname);
  suite_add_tcase(s, tc_core);

  tc_core = tcase_create("test_two");
  tcase_add_test(tc_core, test_two);
  suite_add_tcase(s, tc_core);

  return s;
  }

void rundebug()
  {
  }

int main(void)
  {
  int number_failed = 0;
  SRunner *sr = NULL;
  rundebug();
  sr = srunner_create(get_hostname_suite());
  srunner_set_log(sr, "get_hostname_suite.log");
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return number_failed;
  }
