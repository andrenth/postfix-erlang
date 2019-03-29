Source: postfix
Section: mail
Priority: extra
Maintainer: LaMont Jones <lamont@debian.org>
Standards-Version: 3.7.2.0
Build-Depends: debhelper (>= 4.1.16), po-debconf (>= 0.5.0), groff-base, patch, lsb-release, libdb-dev (>=4.6.19), libldap2-dev (>=2.1), libpcre3-dev, libmysqlclient-dev|libmysqlclient15-dev|libmysqlclient14-dev, libssl-dev (>=0.9.7), libsasl2-dev, libpq-dev, libcdb-dev | tinycdb, hardening-wrapper, erlang-dev
XS-Vcs-Browser: http://git.debian.org/?p=users/lamont/postfix.git
XS-Vcs-Git: git://git.debian.org/~lamont/postfix.git

Package: postfix
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}, netbase, adduser (>=3.48), dpkg (>= 1.8.3), lsb-base (>=3.0-6), ssl-cert
Replaces: postfix-tls, mail-transport-agent
Recommends: python
Suggests: procmail, postfix-mysql, postfix-pgsql, postfix-ldap, postfix-pcre, sasl2-bin, libsasl2-modules, resolvconf, postfix-cdb, mail-reader, ufw
Conflicts: mail-transport-agent, smail, libnss-db (<< 2.2-3), postfix-tls
Provides: mail-transport-agent, postfix-tls, ${postfix:Provides}
Description: High-performance mail transport agent
 ${Description}

Package: postfix-ldap
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}, postfix (= ${binary:Version})
Description: LDAP map support for Postfix
 ${Description}
 .
 This provides support for LDAP maps in Postfix. If you plan to use LDAP maps
 with Postfix, you need this.

Package: postfix-cdb
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}, postfix (= ${binary:Version})
Description: CDB map support for Postfix
 ${Description}
 .
 This provides support for CDB (constant database) maps in Postfix. If you
 plan to use CDB maps with Postfix, you need this.

Package: postfix-pcre
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}, postfix (= ${binary:Version})
Description: PCRE map support for Postfix
 ${Description}
 .
 This provides support for PCRE (perl compatible regular expression) maps in
 Postfix. If you plan to use PCRE maps with Postfix, you need this.

Package: postfix-erlang
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}, postfix (= ${binary:Version})
Description: Erlang map support for Postfix
 ${Description}
 .
 This provides support for Erlang maps in Postfix. If you plan to use Erlang
 maps with Postfix, you need this.

Package: postfix-mysql
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}, postfix (= ${binary:Version})
Description: MySQL map support for Postfix
 ${Description}
 .
 This provides support for MySQL maps in Postfix. If you plan to use MySQL
 maps with Postfix, you need this.

Package: postfix-pgsql
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}, postfix (= ${binary:Version})
Description: PostgreSQL map support for Postfix
 ${Description}
 .
 This provides support for PostgreSQL maps in Postfix. If you plan to use
 PostgreSQL maps with Postfix, you need this.

Package: postfix-dev
Architecture: all
Section: devel
Depends: postfix (>= ${Upstream}-0), postfix (<< ${Upstream}.0-0)
Description: Loadable modules development environment for Postfix
 ${Description}
 .
 This provides the headers and library links to build additional map
 types for Postfix. If you're not developing postfix modules, then you
 do not need this.

Package: postfix-doc
Architecture: all
Section: doc
Suggests: postfix
Replaces: postfix-tls
Description: Documentation for Postfix
 ${Description}
 .
 This package provides the documentation for Postfix.
