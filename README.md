# Simple unix shell

A simplified version unix shell.
For the functionality detail, plz refer to the spec.

```shell=
% ls
bin test.html
% ls bin
cat ls noop number removetag removetag0
% cat test.html > test1.txt
% cat test1.txt
<!test.html>
<TITLE>Test</TITLE>
<BODY>This is a <b>test</b> program
for ras.
</BODY>
% removetag test.html
Test
This is a test program
for ras.
```
