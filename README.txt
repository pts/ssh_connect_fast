ssh_connect_fast.c: ssh(1) trampoline for faster connection setup
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
by pts@fazekas.hu at Mon Oct  9 15:08:14 CEST 2017

ssh_connect_fast is a small C trampoline tool which helps speeding up SSH
connection setup on the client side, by selecting a faster ssh-agent (if
available and already started) and by ignoring system-level options (in
/etc/ssh/ssh_config) for some server hosts.

Compilation (tested with GCC 4.8.4):

  gcc -s -O2 -W -Wall -Wextra -Werror -Werror=missing-declarations ssh_connect_fast.c -o ssh_connect_fast

Compilation with xtiny (for a tiny Linux i386 executable):

  xtiny gcc  -W -Wall -Wextra -Werror -Werror=missing-declarations ssh_connect_fast.c -o ssh_connect_fast

Use ssh_connect_fast instead of ssh, or copy the ssh_connect_fast
executable to somewhere on your $PATH earlier than the real ssh
executable.

License
~~~~~~~
GNU GPL v2 or newer.

Background
~~~~~~~~~~

SSH connection setup (i.e. the combined client-side and server-side work
started with the ssh(1) client program until the starting of the command
or shell on the server) can be slow for many reasons, some of them
because of suboptimal configuration on the client:

R1. ssh-agent is slow to respond. (This can happen in some corporate SSH
    client setup, where the slow corporate tool is running instead of
    ssh-agent.)

R2. The system-level options (in /etc/ssh/ssh_config) are suboptimal.
    This can happen if they add too many IdentityFile directives (which
    are slow to check by the server), or if they specify something slow
    to parse or cumbersome to override in the user-level config
    (~/.ssh/config).

To solve R1, one can run a separate ssh-agent and use it for hosts for
which the default ssh-agent is not necessary. ssh_connect_fast automates
this by expecting the socket filename of that ssh-agent in
$SSH_AUTH_SOCK_FAST, and when it detects this evironment variable and a
compatible server host, it calls ssh(1) with the variable renamed to
$SSSH_AUTH_SOCK.

To solve R2, one can use `ssh -F"$HOME/.ssh/config"' instead of `ssh', so
that ssh(1) won't read the system-level config (/etc/ssh/ssh_config).
ssh_connect_fast automates this by prepending the -F... flag above
automatically to the ssh(1) command line if it detects a compatible
server host.

It's important that these speedups shouldn't be enabled by all server
hosts (e.g. they shouldn't be enabled by corporate servers configured in
/etc/ssh/ssh_config), so ssh_connect_fast uses a whitelist approach: it
only makes ssh(1) invocation changes for server hosts mentioned in
~/.ssh/ssh_config in a line starting with `Host .fast ' (no tabs, just
single spaces as indicated, with possilby some leading spaces at the
beginning of the line). So `Host myhomehost home' should be changed to
`Host .fast myhomehost home' to enable the speedups.

__END__
