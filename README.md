# esphome-flowerpower

Composant ESPHome pour un **Parrot Flower Power** (BLE). Il lit les mesures live et l'historique stocké dans le capteur jusqu'à saturation de sa mémoire.

Pas encore proposé à la communauté ESPHome. Usage privé via `external_components`.

## Ce que le capteur expose

- Température air (°C), température sol (°C), humidité sol (% VWC), lumière (DLI, mol/m²/j), conductivité sol, batterie.
- Firmware ≥ 1.1 : floats calibrés (`FA09`–`FA0D`). Sinon conversion des `uint16` bruts.
- Historique : services `FC00` (index, session, période) et `FB00` (upload par trames de 20 octets, ACK tous les 128). Chaque échantillon fait 12 octets : air, lumière, EC, température sol, VWC, batterie.
- Le capteur coupe la liaison après ~1 s sans requête et n'advertise que toutes les 2 s. Le composant se connecte, lit, puis se déconnecte. Ne pas laisser `auto_connect: true`.

L'EC de l'historique reste une valeur brute : Parrot n'a pas publié la conversion dS/m de ce champ.

## Installation

ESP32 uniquement (classique, S3 ou C3). Un `time:` SNTP est nécessaire pour dater l'historique.

```yaml
external_components:
  - source: github://ogrums/esphome-flowerpower@main
    components: [flowerpower]
    refresh: 1d

esp32_ble_tracker:

ble_client:
  - mac_address: A0:14:3D:00:00:00
    id: fp_client
    auto_connect: false

time:
  - platform: sntp

flowerpower:
  - ble_client_id: fp_client
    id: fp
    update_interval: 30min
    fetch_history: true
    history_publish_limit: 500
    air_temperature:
      name: "Flower Power température air"
    soil_temperature:
      name: "Flower Power température sol"
    soil_moisture:
      name: "Flower Power humidité sol"
    sunlight:
      name: "Flower Power lumière"
    soil_ec:
      name: "Flower Power EC sol"
    battery_level:
      name: "Flower Power batterie"
    history_entries:
      name: "Flower Power échantillons reçus"
    session_period:
      name: "Flower Power période session"
    firmware_revision:
      name: "Flower Power firmware"
    history_sample:
      name: "Flower Power échantillon historique"
```

`history_publish_limit: 0` publie tout le rattrapage (peut faire des centaines d'états Home Assistant). `500` est un plafond par session. L'index déjà synchronisé est mémorisé dans la flash de l'ESP : les sessions suivantes ne reprennent que le delta.

LED, depuis une action :

```yaml
switch:
  - platform: template
    name: "Flower Power LED"
    optimistic: true
    turn_on_action:
      - lambda: 'id(fp).set_led(true);'
    turn_off_action:
      - lambda: 'id(fp).set_led(false);'
```

## Home Assistant

Les capteurs live arrivent par l'API ESPHome et alimentent l'historique HA à partir du moment où ils sont lus.

L'intégration `plant` :

```yaml
plant:
  - name: Basilic
    sensors:
      moisture: sensor.flower_power_humidite_sol
      temperature: sensor.flower_power_temperature_air
      conductivity: sensor.flower_power_ec_sol
      brightness: sensor.flower_power_lumiere
    min_moisture: 15
    max_moisture: 60
```

L'historique interne du capteur n'a pas les bons horodatages si on se contente de `publish_state` (HA date à l'arrivée). Chaque échantillon nouveau est donc publié sur `text_sensor` `history_sample` en JSON :

```json
{"ts":1710000000,"index":1402,"air_c":21.2,"soil_c":19.1,"vwc":32.4,"light":4.2,"ec":1200,"battery":80}
```

`ts` est l'epoch UTC reconstruit avec l'horloge du capteur (`startup = maintenant - horloge`) et la période de session. Une automatisation peut relayer ça :

```yaml
automation:
  - alias: Flower Power historique
    triggers:
      - trigger: state
        entity_id: sensor.flower_power_echantillon_historique
        not_to:
          - unknown
          - unavailable
    actions:
      - event: flowerpower_history
        event_data:
          payload: "{{ trigger.to_state.state }}"
```

Pour un vrai graphe rétrodaté, envoyer ce JSON vers InfluxDB ou VictoriaMetrics (le recorder HA ne réécrit pas le passé). Les lectures suivantes, elles, remplissent l'historique HA normalement.

## Limites connues

- Un ESP32 gère mal plus de 2–3 clients BLE à la fois : interroger les capteurs l'un après l'autre.
- Le format de trame de transport suit l'implémentation de terrain (index little-endian), pas la mention « big endian » ambiguë de la spec Parrot.
- Le corps du fichier historique est big-endian (en-tête 16 octets, échantillon 12 octets), comme les décodeurs publics.
- À valider sur le firmware exact du capteur au premier flash : logger en `DEBUG`.
