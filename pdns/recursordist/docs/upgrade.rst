Upgrade Guide
=============

Before upgrading, it is advised to read the :doc:`changelog/index`.
When upgrading several versions, please read **all** notes applying to the upgrade.

4.3.x to 4.4.0 or master
------------------------

Parsing of unknown record types
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The parsing (from zone files) of unknown records types (of the form
``\# <length> <hex data>``) has been made more strict. Previously, invalid formatted records could produce
inconsistent results.

Deprecated and changed settings
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
- The :ref:`setting-query-local-address` setting has been modified to be able to include both IPv4 and IPv6 addresses.
- The :ref:`setting-query-local-address6` settings is now deprecated.

New settings
^^^^^^^^^^^^
- The :ref:`setting-dns64-prefix` setting has been added, enabling common cases of DNS64 handling without having to write Lua code.
- The :ref:`setting-proxy-protocol-from` and :ref:`setting-proxy-protocol-maximum-size` settings have been added to allow for passing of Proxy Protocol Version 2 headers between a client and the recursor.
- The :ref:`setting-record-cache-shards` setting has been added, enabling the administrator to change the number of shards in the records cache. The value of the metric ``record-cache-contended`` divided by ``record-cache-acquired`` indicates if the record cache locks are contended. If so, increasing the number of shards can help reducing the contention.

4.2.x to 4.3.0
------------------------

Lua Netmask class methods changed
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
- Netmask class methods ``isIpv4`` and ``isIpv6`` have been deprecated in Lua, use :func:`Netmask.isIPv4` and :func:`Netmask.isIPv6` instead. In C++ API these methods have been removed.

``socket-dir`` changed
^^^^^^^^^^^^^^^^^^^^^^
The default :ref:`setting-socket-dir` has changed to include ``pdns-recursor`` in the path.
For non-chrooted setups, it is now whatever is passed to ``--with-socketdir`` during configure (``/var/run`` by default) plus ``pdns-recursor``.
The systemd unit-file is updated to reflect this change and systemd will automatically create the directory with the proper permissions.
The packaged sysV init-script also creates this directory.
For other operating systems, update your init-scripts accordingly.

Systemd service and permissions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The systemd service-file that is installed no longer uses the ``root`` user to start.
It uses the user and group set with the ``--with-service-user`` and ``--with-service-group`` switches during
configuration, "pdns" on Debian and "pdns-recursor" on CentOS by default.
This could mean that PowerDNS Recursor cannot read its configuration, lua scripts, auth-zones or other data.
It is recommended to recursively ``chown`` directories used by PowerDNS Recursor::

  # For Debian-based systems
  chown -R root:pdns /etc/powerdns

  # For CentOS and RHEL based systems
  chown -R root:pdns-recursor /etc/pdns-recursor

Packages provided on `the PowerDNS Repository <https://repo.powerdns.com>`__ will ``chown`` directories created by them accordingly in the post-installation steps.

New settings
^^^^^^^^^^^^
- The :ref:`setting-allow-trust-anchor-query` setting has been added. This setting controls if negative trust anchors can be queried. The default is `no`.
- The :ref:`setting-max-concurrent-requests-per-tcp-connection` has been added. This setting controls how many requests are handled concurrently per incoming TCP connection. The default is 10.
- The :ref:`setting-max-generate-steps` setting has been added. This sets the maximum number of steps that will be performed when loading a BIND zone with the ``$GENERATE`` directive. The default is 0, which is unlimited.
- The :ref:`setting-nothing-below-nxdomain` setting has been added. This setting controls the way cached NXDOMAIN replies imply non-existence of a whole subtree. The default is `dnssec` which means that only DNSSEC validated NXDOMAINS results are used.
- The :ref:`setting-qname-minimization` setting has been added. This options controls if QName Minimization is used. The default is `yes`.
  
4.1.x to 4.2.0
--------------

Two new settings have been added:

- :ref:`setting-xpf-allow-from` can contain a list of IP addresses ranges from which `XPF (X-Proxied-For) <https://datatracker.ietf.org/doc/draft-bellis-dnsop-xpf/>`_ records will be trusted.
- :ref:`setting-xpf-rr-code` should list the number of the XPF record to use (in lieu of an assigned code).

4.0.x to 4.1.0
--------------

:ref:`setting-loglevel` defaulted to 4 but was always overridden to 6 during
the startup. The issue has been fixed and the default value set to 6 to keep the behavior
consistent.

The ``--with-libsodium`` configure flag has changed from 'no' to 'auto'.
This means that if libsodium and its development header are installed, it will be linked in.

4.0.3 to 4.0.4
--------------

One setting has been added to limit the risk of overflowing the stack:

-  :ref:`setting-max-recursion-depth`: defaults to 40 and was unlimited before

4.0.0 to 4.0.1
--------------

Two settings have changed defaults, these new defaults decrease CPU usage:

-  :ref:`setting-root-nx-trust` changed from "no" to "yes"
-  :ref:`setting-log-common-errors` changed from "yes" to "no"
