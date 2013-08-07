#!/bin/bash

export ZEND_DONT_UNLOAD_MODULES=1
export USE_ZEND_ALLOC=0

valgrind --trace-children=yes --leak-check=full --show-possibly-lost=yes --leak-resolution=med /monamour/php/bin/php /home/devel/crepo/php/dumpfatal-extension/test/dumpfatal.php
