# esphome-flowerpower

Composant ESPHome pour un **Parrot Flower Power** et un **Parrot Pot** (BLE). Mesures live, historique interne, et sur le Pot : niveau de cuve et pompe.

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
    # Parrot Pot uniquement (service 39e1F900). Absents sur un Flower Power.
    water_level:
      name: "Parrot Pot niveau cuve"
    watering_status:
      name: "Parrot Pot état arrosage"
    watering_mode:
      name: "Parrot Pot mode arrosage"
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

button:
  - platform: template
    name: "Parrot Pot arroser"
    on_press:
      - lambda: 'id(fp).water_now(8);'
```

## Parrot Pot

Le Pot réutilise le GATT Flower Power et ajoute le service d'arrosage `39e1F900-84a8-11e2-afba-0002a5d5c51b`.

| UUID | Accès | Rôle documenté |
| --- | --- | --- |
| `F907` | read/notify | niveau de cuve, % |
| `F906` | write | déclenchement manuel de la pompe |
| `F912` | read/write | état d'arrosage (valeur brute) |
| `F90D` | read/write | mode d'arrosage (valeur brute) |

Cuve 2,2 L. Seuil bas utilisé par les apps : 15 %. `water_now(8)` écrit un octet sur `F906` (durée en secondes, défaut 1). Ce payload n'est pas dans la spec Parrot publique : à confirmer sur ton Pot. Les modes produit sont Manual, Perfect Drop, Plant Sitter et Custom ; leurs codes numériques ne sont pas publiés, le capteur expose donc la valeur brute.

Les autres caractéristiques `F901`–`F905`, `F908`, `F910`–`F911` sont listées par WatchFlower sans description. Elles ne sont pas écrites.

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
- La pompe n'existe pas sur un Flower Power. Un write `F906` y est ignoré si la caractéristique est absente.
- Reverse : un dump ou un log nRF Connect du Pot peut identifier les octets de `F906` et l'énumération de `F90D`. Le firmware Flower Power seul ne contient pas la pompe.
