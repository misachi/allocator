# Allocator 
Custom dynamic memory allocator using the segragated fit mechanism

Built this as part of [SiKV](https://github.com/misachi/SiKV), that I am also working on, but I felt the need to have it on a separate repo to work on it independently -- as I add more features in the future

# Installation
```
Clone the repository to a local directory e.g /tmp

cd /tmp/allocator  # Replace /tmp to your directory of choice

make install
```

The library will be installed in the `/usr/local/lib` directory. Ensure the user has the right access to `/usr/local/lib` directory

Once installed, the header file can be accessed in the usual way: `<alloc.h>`

# Uninstallation
```
cd /tmp/allocator   # Replace /tmp to your directory of choice
make uninstall
```
...or you can do this

```
rm /usr/local/lib/liballoc.so
rm /usr/local/include/alloc.h
```

