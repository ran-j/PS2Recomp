#include "MiniTest.h"

void register_ps2_vu1_tests();
void register_ps2_vu_pipeline_tests();

void register_ps2_vu_tests();
void reset_ps2_test_function_table();

int main()
{
    MiniTest::BeforeEach(reset_ps2_test_function_table);
    register_ps2_vu1_tests();
    register_ps2_vu_pipeline_tests();

    register_ps2_vu_tests();
    return MiniTest::Run();
}
