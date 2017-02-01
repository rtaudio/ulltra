h=([ [ 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 7 5 20 94 164 208 63 58 109 203 249 72 19 1 7 3 0 ...
 0 0 0 2 6 78 428 571 809 5437 5487 479 128 145 176 281 208 101 49 41 24 11 14 10 6 1 1 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0]' [ 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 2 12 77 176 394 130 49 58 169 382 80 22 12 5 3 1 ...
 0 0 0 0 1 82 414 688 920 4837 4976 505 164 178 158 276 259 115 89 59 42 17 23 16 9 3 2 0 1 0 0 0 0 0 0 0 0 0 0 0 2 0]' [ 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 3 8 9 55 167 400 154 59 44 56 316 98 12 2 4 0 0 ...
 0 0 0 1 3 30 115 664 1484 4761 4795 508 230 175 165 224 328 102 74 40 36 14 9 8 2 2 1 1 1 0 1 0 0 0 0 0 0 0 0 1 1 0]']')';

x=([ 0 0.258925 0.584893 0.995262 1.51189 2.16228 2.98107 4.01187 5.30957 6.94328 9 11.5893 14.8489 18.9526 24.1189 30.6228 38.8107 49.1187 62.0957 78.4328 99 124.893 157.489 198.526 250.189 315.228 397.107 500.187 629.957 793.328 999 1257.93 1583.89 1994.26 2510.89 3161.28 3980.07 5010.87 6308.57 7942.28 9999 12588.3 15847.9 19951.6 25117.9 31621.8 39809.7 50117.7 63094.8 79431.8 ...
 99999 125892 158488 199525 251188 316227 398106 501186 630957 794327 999999 1.25892e+06 1.58489e+06 1.99526e+06 2.51189e+06 3.16228e+06 3.98107e+06 5.01187e+06 6.30958e+06 7.94328e+06 1e+07 1.25893e+07 1.58489e+07 1.99526e+07 2.51189e+07 3.16228e+07 3.98107e+07 5.01187e+07 6.30958e+07 7.94328e+07 1e+08 1.25893e+08 1.58489e+08 1.99526e+08 2.51188e+08 3.16228e+08 3.98108e+08 5.01187e+08 6.30958e+08 7.94328e+08 1e+09 1.25893e+09]');

o=ceil(3.0103);
figure; loglog(x(o:end,:), 1+h(o:end,:), 'LineWidth',2); grid on; 
legend( '64', '128', '512'); title('receiver jitter (ns) flarch <-[udplink(rxblock=KernelBlock,to=40000us,bufs=32) to 192.168.2.112:26100]-> DESKTOP-79T88LQ (id 00:50:56:C0:00:08, host [192.168.2.112]:27212)'); 