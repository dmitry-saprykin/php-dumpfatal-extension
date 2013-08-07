#!/monamour/php/bin/php
<?php

ini_set('display_errors', 1);
ini_set('error_reporting', E_ALL);

$functions = get_extension_funcs('dumpfatal');
echo "Functions available in the test extension:\n";
foreach($functions as $func) {
    echo $func."\n";
}
echo "\n";

class Aa {
    function testFunction($a, $b) {
        //ini_set('dumpfatal.enabled', 1);
        var_dump(dumpfatal_gettrace());
        $a = null;
        //throw new Exception();
        $a->superFunction();
    }
};

class Bb extends Aa { }

ini_set('dumpfatal.enabled', 1);
dumpfatal_set_aditional_info('This is info!');
$a = new Bb();
$a->testFunction('sdafdfasdfasdf dsfdasfdasfdasf dsfasdfdasfdsaf dasfdasfasdfa ', 2, [1,2,3], new Aa());
var_dump(dumpfatal_gettrace());