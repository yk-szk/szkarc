# szkarc
My archiving tools.

## zipdirs
Zip each directory in the input directory.
Zipping is done by zlib-ng and thus faster than average zip tools.


Example
```sh
zipdirs input output --depth 1 --jobs 4
```

## unzipdirs
Invert `zipdirs`.

Example
```sh
unzipdirs input output --depth 1 --jobs 4
```

## deldirs
Delete directories matching specified conditions.

Example
```sh
deldirs input --absent filename
```