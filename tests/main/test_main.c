#include "unity.h"

TEST_CASE("sanity test", "[sanity]")
{
    TEST_ASSERT_TRUE(true);
}

void app_main(void)
{
    unity_run_menu();
}
