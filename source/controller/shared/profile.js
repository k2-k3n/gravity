// Gravity: what the controllers show and the CC each control sends (channel 1).
// src/app.cpp binds the same numbers in kBind.
export const profile = {
  name: 'Gravity',
  accent: '#a8843a',
  pages: [
    {key: 'P', label: 'PLAY', title: 'Suspend the stone', color: '#C9A24B', controls: [
      {id: 'suspend', cc: 30, kind: 'trigger', label: 'HOLD', key: 'A'},
      {id: 'soar', cc: 31, kind: 'trigger', label: 'SOAR', key: 'S'},
      {id: 'sink', cc: 32, kind: 'trigger', label: 'SINK', key: 'D'},
      {id: 'flip', cc: 33, kind: 'trigger', label: 'FLIP', key: 'F'},
      {id: 'space', kind: 'xy', label: 'Float × Mix', x: 'float', y: 'mix'},
    ]},
    {key: 'K', label: 'KEY', title: 'The key', color: '#5E6B5A', controls: [
      {id: 'note', cc: 20, kind: 'choice', label: 'Key', options: ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'], def: 2},
      {id: 'mode', cc: 21, kind: 'choice', label: 'Voicing', options: ['MINOR', 'MAJOR'], def: 0},
      {id: 'float', cc: 22, kind: 'range', label: 'Float', min: 0, max: 100, def: 68},
    ]},
    {key: 'M', label: 'MIX', title: 'The mix', color: '#8C8579', controls: [
      {id: 'mix', cc: 23, kind: 'range', label: 'Mix', min: 0, max: 100, unit: '%', def: 65},
      {id: 'level', cc: 24, kind: 'range', label: 'Level', min: 0, max: 100, unit: '%', def: 82},
      {id: 'dry', cc: 25, kind: 'toggle', label: 'Dry compare', def: 0},
    ]},
  ],
};
