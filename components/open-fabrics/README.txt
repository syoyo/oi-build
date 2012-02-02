What is this?
-------------

OFED 1.5.4.1 port for OpenIndiana(Illumos kernel) by Syoyo Fujita(syoyo@lighttransport.com)

OFED 1.5.4.1 port for OpenIndiana(Illumos) is based on OFED 1.5.3 port for Solaris 11, which is disclosed as open-fabric package under CDDL license and hosted at http://hg.openindiana.org/oi-build. 

Some patches in open-fabrics component expects it work with Solaris 11 kernel, so some fix is required to work with OpenIndiana(Illumos).


Status
------

---------------- ------------ ------------- ----------------------------------
Modules          1.5.3        1.5.4.1       Comments
---------------- ------------ ------------- ----------------------------------
libibverbs       1.1.4-1.22   1.1.4-1.24    Some fix is required for Illumos.
                                            1) Use solaris_compatibility.c from
                                            old ofusr package.
                                            2) Fix sysfs path.
libibumad        1.3.7        1.3.7         sysfs path fix.
libibmad         1.3.7        1.3.8         Hunk success with offset
librdmacm        1.0.14-1     1.0.15        14->15 fix + Illumos fix.
libmlx4          1.0.1-1.18   1.0.1-1.20    No diff.
libmthca         1.0.5-0.1    1.0.5-0.1     No diff.
opensm           3.3.9        3.3.13        TODO
ibutils          1.5.7        1.5.7-0.1     TODO
infiniband-diags 1.5.8        1.5.13        TODO
qperf            0.4.6-1      0.4.6-1       No diff.
perftest         1.3.0-0.42   1.3.0-0.58    TODO
rds-tools        2.0.4        2.0.4         No diff.
libsdp           1.1.108-0.15 1.1.108.0.17  TODO


Note
----

 * setnodedesc feature is disabled(Seems it only works with Solaris 11 kernel).

