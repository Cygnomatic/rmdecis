//
// Created by catslashbin on 23-1-24.
//

#include "basic_aiming_impl.h"
#include "rmdecis/core.h"

using namespace cv;

EulerAngles BasicAiming::BasicAimingImpl::update(ArmorFrameInput detection, cv::Mat *debug_img) {

    std::vector<ArmorInfo> armor_infos;
    for (const auto &armor: detection.armor_info) {
        cv::Rect2i bbox = armor.corners_img.getBoundingBox();
        if (bbox.x <= 0 || bbox.x + bbox.width >= frame_width_
            || bbox.y <= 0 || bbox.y + bbox.height >= frame_height_)
            continue;
        armor_infos.emplace_back(armor);
    }

    reconstructor.reconstructArmors(armor_infos, detection.robot_state);
    tracker.update(armor_infos, detection.seq_idx);

    auto tracks_map = tracker.getTracks();

    if (tracks_map.find(last_aiming_id_) == tracks_map.end()) {
        // Last track lost, update target track
        last_aiming_id_ = chooseNextTarget(tracks_map, detection.seq_idx);
    }

    // Check if there is a target
    if (last_aiming_id_ != -1) {
        auto pred_angle = predictFromTrack(tracks_map.at(last_aiming_id_),
                                           detection.seq_idx + compensate_frame);

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
EulerAngles BasicAiming::BasicAimingImpl::predictFromTrack(ArmorTrack &track, int frame_seq) {

    float horizontal_dist, vertical_dist, yaw, pitch;

    TrackArmorInfo target_info = track.predict(frame_seq);
    Point3f center = target_info.target_world;
    reconstructor.solveAngle(center, &yaw, &horizontal_dist, &vertical_dist);

    // TODO: calcShootAngle can return pitch with nan if there is no solution.
    pitch = compensator.calcShootAngle(ballet_init_speed, horizontal_dist, vertical_dist);

    // FIXME: Return pitch should multiply -1
    return EulerAngles(yaw, pitch);

}

int BasicAiming::BasicAimingImpl::chooseNextTarget(std::map<int, ArmorTrack> &tracks_map, int frame_seq) {

    if (tracks_map.empty()) {
        // No targets found.
        return -1;
    }

    float min_dist = std::numeric_limits<float>::infinity();
    int min_id = -1;

    for (auto &track_pair: tracks_map) {
        EulerAngles pred_angle = predictFromTrack(track_pair.second, frame_seq);

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
          compensator(cfg.get<float>("aiming.basic.airResistanceConst", 0.1)),
          frame_width_(cfg.get<int>("camera.width")),
          frame_height_(cfg.get<int>("camera.height")) {

    // TODO: Time to compensate frame
    compensate_frame = cfg.get<int>("aiming.basic.compensateTime", 0);
}

void BasicAiming::BasicAimingImpl::drawDebugInfo(cv::Mat *debug_img) {

}
