#include "MiniTest.h"

void register_code_generator_tests();
void register_decoder_tests();

int main()
{
    register_code_generator_tests();
    register_decoder_tests();
    return MiniTest::Run();
}
