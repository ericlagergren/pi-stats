# pi-stats
`pi-stats` reads stats from a Raspberry Pi's VideoCore processor and writes them
to stdout using InfluxDB's line protocol[0].

## Installing

To have everything installed for you, run `make all`. You'll likely need root
privileges.

To install `pi-stats` manually:

1. Build the `pi-stats` binary with `make` (or `make install` and skip step #2)
2. Copy `pi-stats` to `/usr/local/bin`
3. Copy `pi-stats.service` to `/etc/systemd/service/` (or `make install-service`)
4. Copy `pi-stats.conf` to `/etc/logrotate.d/pi-stats` (or `make install-logrot`)

Once you've installed `pi-stats`, make sure to enable and run the service with
`systemctl enable pi-stats` and `systemctl start pi-stats`, respectively. Again,
you'll likely need root privileges.

# Configuring

By default, `pi-stats` reads from VideoCore once per second. This can be
configured with the `-s` flag, which is the number of seconds between reads.

# Usage

The stats are written to `/var/log/pi-stats.log`, which can then be parsed by
Telegraf[1].

[0]: https://docs.influxdata.com/influxdb/v1.7/write_protocols/line_protocol_tutorial/
[1]: https://www.influxdata.com/time-series-platform/telegraf/
