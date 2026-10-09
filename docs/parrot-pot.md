# Parrot Pot

Le Pot (produit `2AG61POT`, 4 piles AA, cuve 2,2 L) parle le même GATT que le Flower Power, plus un service d'arrosage.

Service `39e1f900-84a8-11e2-afba-0002a5d5c51b` :

- `39e1f907` niveau de cuve en % (read/notify)
- `39e1f906` déclenchement manuel (write). Le composant écrit la durée en secondes, 1 octet.
- `39e1f912` état d'arrosage, octet brut
- `39e1f90d` mode d'arrosage, octet brut

Modes produit, codes non publiés : Manual, Perfect Drop, Plant Sitter, Custom. Ne pas écrire `F90D` tant que l'énumération n'est pas lue sur un appareil.

Alerte cuve basse des apps : sous 15 %.

```yaml
binary_sensor:
  - platform: template
    name: "Parrot Pot cuve basse"
    lambda: 'return id(pot_water).state < 15;'
```

Home Assistant : le bouton `Parrot Pot arroser` appelle `water_now`. Une automatisation peut l'actionner si l'humidité sol passe sous un seuil et que la cuve est au-dessus de 15 %.
