<?php

namespace Xhprof\Tests;

use PHPUnit\Framework\TestCase;

class TestSuite extends TestCase
{
   public function testClassConstructor()
   {
        $bprof = bprof_enable();
        $this->assertSame(1, 1);
       $result = bprof_disable();
       $this->assertSame(1, $result);
       var_dump($result);
   }
}