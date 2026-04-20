option task = {name: "combsense downsample 1d", every: 6h, offset: 10m}

from(bucket: "combsense_1h")
  |> range(start: -12h, stop: -task.offset)
  |> filter(fn: (r) => r._measurement == "sensor_reading")
  |> aggregateWindow(every: 1d, fn: mean, createEmpty: false)
  |> to(bucket: "combsense_1d", org: "combsense")
