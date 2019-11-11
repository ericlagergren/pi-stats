# pi-stats
`pi-stats` reads stats from a Raspberry Pi's VideoCore processor and writes them
to stdout using InfluxDB's line protocol[0].

## Usage

1. Build the `pi-stats` binary with `make` (or `make install` and skip step #2)
2. Install inside `/usr/local/bin`
3. Copy `pi-stats.service` to `/etc/systemd/service`
4. Copy `pi-stats.conf` to `/etc/logrotate.d`
5. Enable and start the `pi-stats` service: `systemctl enable pi-stats` and 
   `systemctl start pi-stats`, respectively

By default, `pi-stats` reads from VideoCore once per second. This can be
configured with the `-s` flag, which is the number of seconds between reads.

[0]: https://docs.influxdata.com/influxdb/v1.7/write_protocols/line_protocol_tutorial/
