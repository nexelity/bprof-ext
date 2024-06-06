<?php

namespace Bprof\Tests;

function runTest($testFunction, $description) {
    echo "Running test: $description\n";
    $testFunction();
    echo "Test passed: $description\n";
}

function testLongFunctionName() {
    \bprof_enable();

    function exampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexamp12345() {
        usleep(1);
    }

    exampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexamp12345();

    $result = \bprof_disable();

    if (!isset($result['main()>>>Bprof\Tests\exampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexampleexample'])) {
        throw new \Exception('Long function name test failed');
    }
}

function testBasicProfiling() {
    \bprof_enable();

    function foo() {
        usleep(1);
    }

    foo();

    $result = \bprof_disable();

    if (!isset($result['main()>>>Bprof\Tests\foo'])) {
        throw new \Exception('Basic profiling test failed');
    }
}

function testNestedFunctions() {
    \bprof_enable();

    function bar() {
        function baz() {
            usleep(1);
        }
        baz();
    }

    bar();

    $result = \bprof_disable();

    if (!isset($result['main()>>>Bprof\Tests\bar']) || !isset($result['Bprof\Tests\bar>>>Bprof\Tests\baz'])) {
        throw new \Exception('Nested functions test failed');
    }
}

function testNamespaceFunction() {
    \bprof_enable();

    function nsFunc() {
        usleep(1);
    }

    nsFunc();

    $result = \bprof_disable();

    if (!isset($result['main()>>>Bprof\Tests\nsFunc'])) {
        throw new \Exception('Namespace function test failed');
    }
}

function testRecursiveFunction() {
    \bprof_enable();

    function recurse($count) {
        if ($count > 0) {
            recurse($count - 1);
        }
    }

    recurse(5);

    $result = \bprof_disable();

    if (!isset(
        $result['main()>>>Bprof\Tests\recurse'],
        $result['Bprof\Tests\recurse>>>Bprof\Tests\recurse@1'],
        $result['Bprof\Tests\recurse@1>>>Bprof\Tests\recurse@2'],
        $result['Bprof\Tests\recurse@2>>>Bprof\Tests\recurse@3'],
        $result['Bprof\Tests\recurse@3>>>Bprof\Tests\recurse@4'],
        $result['Bprof\Tests\recurse@4>>>Bprof\Tests\recurse@5'],
    )) {
        throw new \Exception('Recursive function test failed');
    }
}

function testBuiltinFunctions() {
    \bprof_enable();

    function builtinFuncTest() {
        strlen("test");
        array_sum([1, 2, 3]);
    }

    builtinFuncTest();

    $result = \bprof_disable();

    if (!isset(
        $result['main()>>>Bprof\Tests\builtinFuncTest'],
        $result['Bprof\Tests\builtinFuncTest>>>strlen'],
        $result['Bprof\Tests\builtinFuncTest>>>array_sum']
    )) {
        throw new \Exception('Built-in functions test failed');
    }
}

function testExceptionHandling() {
    \bprof_enable();

    function exceptionTest() {
        try {
            throw new \Exception("Test exception");
        } catch (\Exception $e) {
            // Handle exception
        }
    }

    exceptionTest();

    $result = \bprof_disable();

    if (!isset($result['main()>>>Bprof\Tests\exceptionTest'])) {
        throw new \Exception('Exception handling test failed');
    }
}

function testMultipleFunctions() {
    \bprof_enable();

    function func1() {
        usleep(1);
    }

    function func2() {
        usleep(1);
    }

    func1();
    func2();

    $result = \bprof_disable();

    if (!isset(
        $result['main()>>>Bprof\Tests\func1'],
         $result['main()>>>Bprof\Tests\func2']
     )) {
        throw new \Exception('Multiple functions test failed');
    }
}

// Run the tests
try {
    runTest('Bprof\Tests\testBasicProfiling', 'Basic profiling test');
    runTest('Bprof\Tests\testLongFunctionName', 'Long function name test');
    runTest('Bprof\Tests\testNestedFunctions', 'Nested functions test');
    runTest('Bprof\Tests\testNamespaceFunction', 'Namespace function test');
    runTest('Bprof\Tests\testRecursiveFunction', 'Recursive function test');
    runTest('Bprof\Tests\testBuiltinFunctions', 'Built-in functions test');
    runTest('Bprof\Tests\testExceptionHandling', 'Exception handling test');
    runTest('Bprof\Tests\testMultipleFunctions', 'Multiple functions test');
    echo "All tests passed!\n";
} catch (\Exception $e) {
    echo $e->getMessage() . "\n";
    exit(1);
}