# ESP 32 skeleton

## configure

```
export IDF_TOOLCHAIN=clang
idf.py fullclean
idf.py set-target esp32s3
```

## dev

Before pushing, please manually run

```
idf.py save-defconfig
idf.py clang-check --exit-code --run-clang-tidy-options="-header-filter=''" main/main.cpp
```
