# Point Cloud / Navigation Investigation

Date: 2026-04-09

## TODO

- [ ] Re-run the same topic measurements immediately after the user reproduces the visible stutter in RViz, to capture a fresh side-by-side dataset with the exact same scene.
- [ ] Compare RViz display settings and fixed frame (`map` vs `odom`) during the stutter to separate rendering stutter from transport slowdown.
- [ ] If needed, run an A/B test with `use_composition:=False` on the user's exact map to isolate container scheduling effects from `small_gicp` compute cost.

## DONE

- [x] Established an investigation plan focused on raw lidar, processed point cloud topics, navigation load, and localization / TF side effects.
- [x] Confirmed a localization/navigation stack is currently running for live measurement.
- [x] Confirmed the relevant live topics are `/livox/lidar`, `/livox/lidar/pointcloud`, `/registered_scan`, and `/terrain_map`.
- [x] Measured live topic rates while navigation/localization was running:
  - `/livox/lidar/pointcloud`: ~20.0 Hz
  - `/registered_scan`: ~20.0 Hz
  - `/terrain_map`: ~20.0 Hz
- [x] Measured live bandwidth for `/livox/lidar/pointcloud`: about 5.24 MB/s, with mean message size about 0.26 MB.
- [x] Verified from source that Livox `PointCloud2` uses packed `LivoxPointXyzrtlt` points, which are 26 bytes each.
- [x] Computed that a 0.26 MB Livox point cloud frame corresponds to about 10,485 points per message, which matches the observed "10000+ points in one frame".
- [x] Confirmed this point count is consistent with normal 20 Hz MID360 output and is not, by itself, evidence of abnormal slowdown.
- [x] Verified from topic graph that `/registered_scan` is consumed by `small_gicp_relocalization`, `terrain_analysis`, `terrain_analysis_ext`, and `sensor_scan_generation`, so this branch has several downstream consumers once navigation is active.
- [x] Verified from a live CPU snapshot during localization/navigation that `nav2_container` could exceed 300% CPU and that `small_gicp_relocalization` on separate-process runs could exceed 250% CPU, indicating compute pressure remains a strong stutter candidate.
- [x] Cross-referenced earlier runtime evidence showing `small_gicp_relocalization` loading a large prior map and repeatedly emitting `GICP did not converge` together with `global_costmap` message-filter drops, which better explains navigation stutter than raw lidar message size alone.
- [x] Captured a fresh live snapshot during the user's reported "RViz visually updates about once per second" moment.
- [x] Verified at that moment that the transport rates were still healthy:
  - `/livox/lidar/pointcloud`: ~20.0 Hz
  - `/registered_scan`: ~20.0 Hz
  - `/terrain_map`: ~20.1 Hz
  - `/odometry`: ~20.0 Hz
  - `/tf` aggregate stream: ~78 Hz
- [x] Verified from live TF sampling that the important dynamic transforms were not actually running at 1 Hz:
  - `base_footprint`: ~20 Hz
  - `gimbal_yaw_fake`: ~50 Hz
  - wheel / gimbal joint frames: ~10 Hz
- [x] Found a more important TF anomaly during the same snapshot: `map -> odom` was not available even though localization/navigation nodes were present.
- [x] Confirmed the navigation bringup remained very CPU-heavy during that snapshot, with `nav2_container` around 360% CPU and RViz around 45% CPU.

## Interim Conclusion

- The observed "10000+ points in one message" is expected for the current Livox driver configuration and message format.
- The live measurements do not support the hypothesis that the lidar topics are dropping to abnormally low transport frequency; the sampled topics stayed near 20 Hz.
- The later "RViz only seems to update once per second" snapshot also did not show a true 1 Hz slowdown in transport or in the key `odom -> base_footprint` TF.
- The strongest fresh anomaly in that snapshot was the absence of `map -> odom`, which can make the navigation view appear unstable or frozen depending on RViz fixed frame and localization state.
- The stronger root-cause candidates remain:
  - heavy localization compute, especially `small_gicp_relocalization`
  - contention inside composed navigation bringup
  - TF / costmap timing issues after localization begins, especially when `map -> odom` is missing or unstable
