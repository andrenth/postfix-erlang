Postfix-Erlang
==============

This is a dictionary type allowing an Erlang RPC call to be made from Postfix.
One use-case would be querying a Mnesia database of alias maps.

The easiest way to use this is from a Debian-based Linux distribution, as they
enable dynamic loading of map types, thus not requiring one to maintain his own
Postfix port.

Building
--------

Drop the `dict_erlang.c` and `dict_erlang.h` files in the `src/global` directory
in the Postfix source. Also copy the appropriate `Makefile.in.[23].[36789]` file
to the same directory according to your Postfix version, renaming it to just
`Makefile.in`. For Postfix 3.[36], copy the `postfix-files.3.[36]` file to the `conf`
directory in the Postfix source.

Note: the Makefiles above were tested with Postfix versions 2.7.0, 2.7.1,
2.8.5, 2.96, 3.3.0 and 3.6.4 (more specifically, the packages from Ubuntu
10.04, 10.10, 11.10, 12.04, 18.04 and 22.04). You may need to adapt it for
different versions.

If you're on a Debian-based distribution, copy the files under `debian` to the
`debian` directory in the Postfix source (the `control.x.y` and `rules.x.y`
files should be renamed to `control` and `rules` to overwrite the standard
debian files). The `postfix-erlang.files.2.x` file should be renamed to
`postfix-erlang.files` for Postifx 2.x or ignored otherwise.
Then, change to the Postfix source directory and run

    $ apt-get build-dep postfix
    $ apt-get install erlang-dev
    $ dpkg-buildpackage -b -uc -us

to generate the `postfix-erlang` package.

If you're using the stock Postfix, you'll need to edit the
`src/global/mail_dict.c` file and add the following code:

     static const DICT_OPEN_INFO dict_open_info[] = {
     ....
    +#ifdef HAS_ERLANG
    +    DICT_TYPE_ERLANG, dict_erlang_open,
    +#endif
         0,
     };

and then compile Postfix with

    $ make makefiles CCARGS="-DHAS_ERLANG -I/path/to/erl_interface/include" \
           AUXLIBS="-L/path/to/erl_interface/lib -lerl_interface_st -lei_st"


Configuration
-------------

Point postfix to the Erlang map like you'd do with other dictionary types.
For example,

    virtual_alias_maps = proxy:erlang:/etc/postfix/virtual_alias_maps.cf

The configuration file requires four parameters:

    nodes = mynode@myhost, mynode@myotherhost
    cookie = chocolate
    module = mydb
    function = aliases

With this setup, postfix-erlang will connect to `mynode@myhost` using
`chocolate` as the magic cookie, and call `mydb:aliases/1`. The argument will
be passed to Erlang as a bit string. The specified function should return
`{ok, [Bitstring, ...]}` on success or `not_found` on failure. Currently there
is no way to choose different function arguments or alternative return formats.
