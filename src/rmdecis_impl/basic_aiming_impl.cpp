//
// Created by catslashbin on 23-1-24.
//

#include "basic_aiming_impl.h"

using namespace cv;

EulerAngles BasicAiming::BasicAimingImpl::update(FrameInput detection) {

    reconstructor.reconstructArmor(detection);
    tracker.update(detection);

    auto tracks_map = tracker.getTracks();

    if (tracks_map.find(last_aiming_id_) == tracks_map.end()) {
        // Last track lost, update target track
        last_aiming_id_ = chooseNextTarget(tracks_map, detection.time);
    }

    // Check if there is a target
    if (last_aiming_id_ != -1) {
        auto pred_angle = predictFromTrack(tracks_map.at(last_aiming_id_), detection.time + compensate_time);

        // Avoid nan from predictFromTrack
        if (!(std::isnan(pred_angle.yaw) || std::isnan(pred_angle.pitch))) {
            last_aiming_angle_ = pred_angle;
        } else {
            warn("Got nan from predictFromTrack!");
        }
    }

    return last_aiming_angle_;

}

/**
 * WARNING: The return of this func can be nan!
 */
EulerAngles BasicAiming::BasicAimingImpl::predictFromTrack(ArmorTrack &track, Time predTime) {

    float horizontal_dist, vertical_dist, yaw, pitch;

    TrackArmorInfo target_info = track.predict(predTime);
    Point3f center = opencvToRep(target_info.center_gimbal);
    Reconstructor::solveDistAndYaw(center, yaw, horizontal_dist, vertical_dist);

    // TODO: calcShootAngle can return pitch with nan if there is no solution.
    pitch = compensator.calcShootAngle(ballet_init_speed, horizontal_dist, vertical_dist);

    return EulerAngles(yaw, pitch);

}

int BasicAiming::BasicAimingImpl::chooseNextTarget(std::map<int, ArmorTrack> &tracks_map, Time &predTime) {

    if (tracks_map.empty()) {
        // No targets found.
        return -1;
    }

    float min_dist = std::numeric_limits<float>::infinity();
    int min_id = -1;

    for (auto &track_pair: tracks_map) {
        EulerAngles pred_angle = predictFromTrack(track_pair.second, predTime);

        // Catch nan from predictFromTrack
        if (std::isnan(pred_angle.yaw) || std::isnan(pred_angle.pitch)) {
            warn("Get nan from predictFromTrack!");
        }

        // TODO: Find a better way to select next target than calculating "distance" between two angles.
        float dist = sqrt(powf(pred_angle.yaw - last_aiming_angle_.yaw, 2)
                          * powf(pred_angle.pitch - last_aiming_angle_.pitch, 2));

        if (dist < min_dist) {
            min_dist = dist;
            min_id = track_pair.second.tracking_id;
        }
    }

    if (min_id != -1)
        debug("New target. Track ID: {}", min_id);

    return min_id;

}

BasicAiming::BasicAimingImpl::BasicAimingImpl(Config &cfg)
        : reconstructor(cfg), tracker(cfg),
          compensator(cfg.get<float>("aiming.basic.airResistanceConst", 0.1)) {

    compensate_time = cfg.get<float>("aiming.basic.compensateTime", 0.0);
}