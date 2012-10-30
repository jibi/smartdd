smartdd
=============

just like dd, but smarter: it copies only blocks that differs to increase flash
life.

Usage
-------
```
smartdd if=inputfile of=outputfile bs=blocksize
```

you can use std input

```
gunzip image.gz | smartdd of=outfile bs=blocksize 
```

and you can redirect to std output (not really usefull, no smart mode enabled)

```
smartdd if=inputfile > outputfile
```
