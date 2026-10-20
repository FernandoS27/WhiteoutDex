# Kollisionsformen

Kollisionsformen sagen dem Spiel, wo der Spieler das Modell **anklicken** kann. Im Spiel sind sie unsichtbar. Einheiten haben meist Kugeln, Gebäude eine Box.

Erstelle sie über **Create > Helpers > Warcraft III Plugins > Wc3 CollisionSphere** oder **Wc3 CollisionBox**. Verbinde sie mit dem Knochen, der sie bewegen soll.

## Beide Formen

[[shot:obj_collision_sphere_r1|Allgemeine Funktionen.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:3]] | Klicke hierauf und dann auf ein Objekt. Die Form springt zu diesem Objekt und wird mit ihm verbunden. |

## Kugel

[[shot:obj_collision_sphere_r2|Kollisionskugel-Parameter.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:6]] | Der Radius der Kugel. |

## Box

[[shot:obj_collision_box_r2|Kollisionsbox-Parameter.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:6]] | Länge der Box. |
| [[ctl:10]] | Breite der Box. |
| [[ctl:14]] | Höhe der Box. |

!!! tip
    [[t:anim_one_click_collision_spheres_btn]] und [[t:anim_one_click_collision_box_btn]] in *Anim: Allgemeine Werkzeuge* erstellen Formen, die zum Modell passen.
