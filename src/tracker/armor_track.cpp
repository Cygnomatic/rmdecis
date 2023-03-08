//
// Created by catslashbin on 22-11-21.
//

#include "armor_track.h"
#include "track_kalman.h"
#include "rmdecis/core.h"
#include "reconstructor/reconstructor.h"

using namespace cv;

ArmorTrack::ArmorTrack(int tracking_id, const ArmorInfo &detection, int frame_seq, Config &cfg)
        : track_kalman(TrackKalman(cfg, detection, frame_seq)), tracking_id(tracking_id) {
    init(detection);
}

void ArmorTrack::init(const ArmorInfo &detection) {
    id_cnt[detection.facility_id]++;
    last_bbox_ = cv::minAreaRect((std::vector<Point2f>) detection.corners_img);
    last_center_proj_ = detection.reconstructor->cam2img(detection.target_cam);
}


void ArmorTrack::correct(const ArmorInfo &detection, int frame_seq) {
    track_kalman.correct(detection, frame_seq); // pre(t-1, t) -> post(t, t)
    id_cnt[detection.facility_id]++;

    last_bbox_ = cv::minAreaRect((std::vector<Point2f>) detection.corners_img);
    last_center_proj_ = detection.reconstructor->cam2img(detection.target_cam);
}

TrackArmorInfo ArmorTrack::predict(int frame_seq) {
    return track_kalman.predict(frame_seq);
}

float ArmorTrack::calcSimilarity(const ArmorInfo &detection, int frame_seq) {

    assert(detection.reconstructor != nullptr);
    Point2f pred_center_proj = detection.reconstructor->cam2img(
            detection.reconstructor->transformer.worldToCam(predict(frame_seq).target_world));

    auto pred_bbox = RotatedRect(((last_bbox_.center - last_center_proj_) + pred_center_proj),
                                 last_bbox_.size, last_bbox_.angle);

    float iou = calculateIoU(last_bbox_, pred_bbox); // minAreaRect(std::vector<Point2f>(detection.corners_img)));
    float id_similarity = calcIdSimilarity(detection.facility_id);
    float center_dist_similarity = calcCenterDistSimilarity(detection.target_world);

    float ret = iou * 0.75 + id_similarity * 0.0 + center_dist_similarity * 0.25;
    assert(!isnanf(ret));

    return ret;
}

float ArmorTrack::calcIdSimilarity(FacilityID id) {
    int sum = std::accumulate(id_cnt.begin(), id_cnt.end(), 2);
    return (float) (id_cnt[id] + 1) /
           (float) sum; // Here we artificially add one positive and one negative. To smoothen the result.
}

float ArmorTrack::calcCenterDistSimilarity(const Point3f &center) {
    Point3f center_pred = {track_kalman.kf.statePost.at<float>(STATE_X),
                           track_kalman.kf.statePost.at<float>(STATE_Y),
                           track_kalman.kf.statePost.at<float>(STATE_Z)};
    float dist = float(norm((center_pred - center)));
    return max((200.f - dist) * 0.005f, 0.f);
}