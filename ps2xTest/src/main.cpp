#include "MiniTest.h"

void register_code_generator_tests();
void register_r5900_decoder_tests();
void register_elf_analyzer_tests();

int main()
{
    register_code_generator_tests();
    register_r5900_decoder_tests();
    register_elf_analyzer_tests();
    return MiniTest::Run();
}
