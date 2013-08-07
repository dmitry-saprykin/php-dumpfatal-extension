#php-dumpfatal-extension

Php extension to make trace for fatal errors.
It tracks all php calls and makes its own copy of stacktrace.
Overhead is about 15% of script execution time. So its useful
to install dumpfatal.so on one server of your farm to get traces.

Extension works just for fatal errors without stack trace.
Anything else including uncaught exceptions is processed as usual.

##ini settings

<b>dupmpfatal.dateformat</b>

Trace header date format. Default - [Y-m-d H:i:s]

<b>dupmpfatal.enabled</b>

Default - 0.

ini_set('dupmpfatal.enabled', 1);<br>
 - if extension was disabled it will make current trace copy and begin execution tracking

ini_set('dupmpfatal.enabled', 0);<br>
 - if extension was enabled it will stop execution tracking<br>
 - after this overhead is ~0

##Functions


<b>array dumpfatal_gettrace()<b>

Returns current trace as array.

<b>void dumpfatal_set_aditional_info(string $info)<b>

Sets trace additional info. 

## Trace format

<pre>
[fatal.date.formatted] php.error.message
Stack trace:
    FILE                                                                            LINE
# 0 {file.name}:{file.line} &lt;{oblect.class.name}&gt; {method.class.name}->{method.name}({parameters})
# 1 {file.name}:{file.line} {method.class.name}::{method.name}({parameters)
# 2 {file.name}:{file.line} {function.name}({parameters})
# 3 {file.name}:{file.line} &lt;{oblect.class.name}&gt; {method.class.name}->{method.name}({parameters})
......
#n {entry.point.filename}:0                               {main}
{trace.additional.info}
</pre>