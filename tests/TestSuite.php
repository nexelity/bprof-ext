<?php

namespace Xhprof\Tests;
bprof_enable();
// exit(1);
echo("hello world");
$result = bprof_disable();


var_dump($result);