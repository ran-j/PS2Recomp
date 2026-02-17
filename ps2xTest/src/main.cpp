#include "MiniTest.h"

void register_code_generator_tests();
void register_r5900_decoder_tests();
void register_elf_analyzer_tests();
void register_ps2_runtime_io_tests();

int main()
{
    register_code_generator_tests();
    register_r5900_decoder_tests();
    register_elf_analyzer_tests();
    register_ps2_runtime_io_tests();
    return MiniTest::Run();
}
