# 2. Broker on the server

This step installs Mosquitto on the Linode test server (Fedora). It gives you two TLS ports with a Let's Encrypt certificate:

- **8883** for the bulbs (`mqtts://`)
- **8084** for the app (`wss://`)

## Before you start

- A DNS **A record** `mqtt.apicov.xyz → 172.104.159.186`. Check it with `getent hosts mqtt.apicov.xyz`.
- SSH access as `tsp`, with sudo.

## Run the setup

```sh
ssh -t tsp@172.104.159.186
git clone git@github.com:apicov/smart_bulbs.git ~/code/smart_bulbs     # first time
cd ~/code/smart_bulbs && git pull                                    # later updates
sudo bash broker/setup-server.sh
```

The script is safe to re-run, and it leaves the existing Apache sites alone. It finishes with a self-test that should end in `✓ TLS login as app on 8883 works` and `✓ anonymous access refused`.

## Users and passwords

Each bulb has its own MQTT user, and the **username is the bulb id**. The access rules (ACL) only let a bulb read and write its own topics.

```sh
sudo cat /root/pico-bulbs-credentials.txt      # app + living-room passwords
sudo broker/add-bulb.sh kitchen                # new bulb → prints its id + password
```

- The `app` password goes into the app's Settings page.
- A bulb's password goes into that bulb's `firmware/sdkconfig.defaults.local`.

For the details, troubleshooting and the manual steps, see [broker/README.md](../broker/README.md).
