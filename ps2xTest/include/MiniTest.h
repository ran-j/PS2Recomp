#pragma once
#include <functional>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <exception>

class TestCase;
using TestRunnerCallback = std::function<void(TestCase&)>;
using TestBeforeCallback = std::function<void()>;

class TestCase
{
private:
    std::map<std::string, TestRunnerCallback> m_cases;
    std::vector<std::string> m_failReason;

    TestBeforeCallback m_beforeEach;
    TestBeforeCallback m_afterEach;
    TestBeforeCallback m_before;
    TestBeforeCallback m_after;

public:
    void Run(const std::string& testName, TestRunnerCallback fn)
    {
        m_cases[testName] = fn;
    }

    template<typename T, typename B>
    inline void Equals(const T& a, const B& b, const std::string& message)
    {
        if (!(a == b))
        {
            m_failReason.emplace_back(message);
        }
    }

    inline void IsTrue(bool condition, const std::string& message)
    {
        if (!condition)
        {
            m_failReason.emplace_back(message);
        }
    }

    inline void IsFalse(bool condition, const std::string& message)
    {
        if (condition)
        {
            m_failReason.emplace_back(message);
        }
    }

    inline void IsNull(const void* ptr, const std::string& message)
    {
        if (ptr != nullptr)
        {
            m_failReason.emplace_back(message);
        }
    }

    inline void IsNotNull(const void* ptr, const std::string& message)
    {
        if (ptr == nullptr)
        {
            m_failReason.emplace_back(message);
        }
    }

    inline void Fail(const std::string& message)
    {
        m_failReason.emplace_back(message);
    }

    void BeforeEach(const TestBeforeCallback& fn)
    {
        m_beforeEach = fn;
    }

    void AfterEach(const TestBeforeCallback& fn)
    {
        m_afterEach = fn;
    }

    void Before(const TestBeforeCallback& fn)
    {
        m_before = fn;
    }

    void After(const TestBeforeCallback& fn)
    {
        m_after = fn;
    }

    void ClearFailures()
    {
        m_failReason.clear();
    }

    friend class MiniTest;
};

using TestCaseCallback = std::function<void(TestCase&)>;

class MiniTest
{
private:
    inline static std::map<std::string, TestCaseCallback> m_cases;

public:
    static void Case(const std::string& caseName, const TestCaseCallback& fn)
    {
        m_cases[caseName] = fn;
    }

    static int Run()
    {
        int failedCount = 0;
        int totalTests = 0;

        for (auto& c : m_cases)
        {
            const std::string& suiteName = c.first;
            const TestCaseCallback& suiteCallback = c.second;

            std::cout << "\n[Suite]: " << suiteName << std::endl;

            TestCase testCase;
            suiteCallback(testCase);

            if (testCase.m_before)
            {
                testCase.m_before();
            }

            for (auto& cc : testCase.m_cases)
            {
                const std::string& testName = cc.first;
                const TestRunnerCallback& testFn = cc.second;

                totalTests++;
                testCase.ClearFailures();

                if (testCase.m_beforeEach)
                {
                    testCase.m_beforeEach();
                }

                try
                {
                    std::cout << "\033[33m" << "  [Run]: " << "\033[0m" << testName << " ";
                    testFn(testCase);

                    if (!testCase.m_failReason.empty())
                    {
                        failedCount++;
                        std::cout << "\033[31m" << "  [Failed]" << "\033[0m" << std::endl;
                        for (const auto& reason : testCase.m_failReason)
                        {
                            std::cerr << "      - " << reason << std::endl;
                        }
                    }
                    else
                    {
                        std::cout << "\033[32m" << "  [Passed]" << "\033[0m" << std::endl;
                    }
                }
                catch (const std::exception& ex)
                {
                    std::cout << "  [Error]: " << ex.what() << std::endl;
                    failedCount++;
                }
                catch (...)
                {
                    failedCount++;
                    std::cerr << "  [Error]: <unknown>" << std::endl;
                }

                if (testCase.m_afterEach)
                {
                    testCase.m_afterEach();
                }
            }

            if (testCase.m_after)
            {
                testCase.m_after();
            }
        }

        std::cout << "\n========================================" << std::endl;
        std::cout << "Total Tests: " << totalTests << std::endl;
        std::cout << "Passed: " << (totalTests - failedCount) << std::endl;
        std::cout << "Failed: " << failedCount << std::endl;
        std::cout << "========================================" << std::endl;

        return failedCount;
    }
};