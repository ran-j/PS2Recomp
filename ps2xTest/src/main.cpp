#include "MiniTest.h"

void register_code_generator_tests();
void register_r5900_decoder_tests();
void register_runtime_syscall_tests();

int main()
{
    register_code_generator_tests();
    register_r5900_decoder_tests();
    register_runtime_syscall_tests();
    return MiniTest::Run();
}
