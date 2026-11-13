// Ghost Note: what the controllers show and the CC each control sends (channel 1).
// src/app.cpp binds the same numbers in kBind.
export const profile = {
  name: 'Ghost Note',
  accent: '#7d6bd8',
  pages: [
    {key: 'P', label: 'PLAY', title: 'Haunt it', color: '#A99BF0', controls: [
      {id: 'boo', cc: 30, kind: 'trigger', label: 'BOO', key: 'A'},
      {id: 'wail', cc: 31, kind: 'trigger', label: 'WAIL', key: 'S'},
      {id: 'high', cc: 32, kind: 'trigger', label: 'HIGH', key: 'D'},
      {id: 'vanish', cc: 33, kind: 'trigger', label: 'VANISH', key: 'F'},
      {id: 'night', kind: 'xy', label: 'Haunt × Fog', x: 'haunt', y: 'fog'},
    ]},
    {key: 'G', label: 'GHOST', title: 'The ghost', color: '#E8E2C4', controls: [
      {id: 'haunt', cc: 20, kind: 'range', label: 'Haunt', min: 0, max: 100, unit: '%', def: 60},
      {id: 'interval', cc: 21, kind: 'choice', label: 'Sings', options: ['UNISON', 'OCTAVE', 'OCT+5TH'], def: 1},
      {id: 'shiver', cc: 23, kind: 'range', label: 'Shiver', min: 0, max: 100, def: 25,
        format: v => `${v < 43 ? 'Calm' : v < 80 ? 'Shaky' : 'Terrified'} · ${v}`},
      {id: 'glide', cc: 24, kind: 'range', label: 'Glide', min: 0, max: 100, unit: '%', def: 45},
    ]},
    {key: 'F', label: 'FOG', title: 'The graveyard', color: '#7FB7A4', controls: [
      {id: 'fog', cc: 22, kind: 'range', label: 'Fog', min: 0, max: 100, unit: '%', def: 45},
      {id: 'dry', cc: 25, kind: 'range', label: 'Guitar', min: 0, max: 100, unit: '%', def: 100},
      {id: 'level', cc: 26, kind: 'range', label: 'Level', min: -12, max: 6, unit: 'dB', def: 0},
    ]},
  ],
};
