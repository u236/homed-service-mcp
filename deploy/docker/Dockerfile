FROM    127.0.0.1:5000/homed-service:latest
ARG     TARGETARCH
ADD     share /usr/share/homed-mcp
RUN     mkdir /etc/homed
RUN     ln -s /data/homed-mcp.conf /etc/homed/homed-mcp.conf
RUN     ln -s /data/homed.log /var/log/homed.log
COPY    homed-mcp-$TARGETARCH /usr/bin/homed-mcp
CMD     ["/usr/bin/homed-mcp"]
