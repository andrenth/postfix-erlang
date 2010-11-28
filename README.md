Postfix-Erlang
==============

This is a dictionary type allowing an Erlang RPC call to be made from Postfix.
One use-case would be querying a Mnesia database of alias maps.

The easiest way to use this is from a Debian-based Linux distribution, as they
enable dynamic loading of map types, thus not requiring one to maintain his own
Postfix port.

Building
--------

Drop the <tt>dict_erlang.c</tt>, <tt>dict_erlang.h</tt> and <tt>Makefile.in</tt>
files in <tt>src/global</tt> in the Postfix source. Note: the Makefile is valid
for Postfix 2.7.0. You may need to adapt it for different versions.

If you're on a Debian-based distribution, copy the files under <tt>debian</tt>
to the <tt>debian</tt> directory in the Postfix source, then run

    $ dpkg-buildpackage -b -uc -us

to generate the <tt>postfix-erlang</tt> package.

If you're using the stock Postfix, you'll need to edit the
<tt>src/global/mail_dict.c</tt> file and add the following code:

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

    node = mynode@mymachine
    cookie = chocolate
    module = mydb
    function = aliases

With this setup, postfix-erlang will connect to <tt>mynode@mymachine</tt> using
<tt>chocolate</tt> as the magic cookie, and call <tt>mydb:aliases/1</tt>. The
argument will be passed to Erlang as a bit string. The specified function
should return <tt>{ok, [Bitstring, ...]}</tt> on success or <tt>not_found</tt>
on failure. Currently there is no way to choose different function arguments or
alternative return formats.
