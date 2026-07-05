# Minimal OpenSSH server for local SFTP/SCP transfer testing (M18 bench).
# Includes iproute2 so tests can inject WAN-like latency via `tc netem` to
# exercise the request-pipelining / disk-overlap paths that a zero-latency
# loopback connection would otherwise hide.
FROM debian:stable-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends openssh-server iproute2 \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /run/sshd \
    && useradd -m -s /bin/bash termsync \
    && echo 'termsync:termsync' | chpasswd \
    && printf 'PasswordAuthentication yes\nPermitRootLogin no\nMaxSessions 64\nMaxStartups 64:30:128\n' \
        > /etc/ssh/sshd_config.d/termsync.conf \
    && mkdir -p /home/termsync/upload \
    && chown -R termsync:termsync /home/termsync

EXPOSE 22
CMD ["/usr/sbin/sshd", "-D", "-e"]
