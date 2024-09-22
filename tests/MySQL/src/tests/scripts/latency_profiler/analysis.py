import numpy as np


def partition_log_template(file_path, class_1, class_2):
    with open(file_path, "r") as file:
        lines = file.readlines()

        current_type = None
        current_request = {class_1: [], class_2: []}
        ret = []

        for line in lines:
            parts = line.strip().split()
            event = parts[1]

            if class_1 in event:
                event_type = class_1
            elif class_2 in event:
                event_type = class_2
            else:
                continue

            if event_type != current_type and event_type == class_1:
                ret.append(current_request)
                current_request = {class_1: [], class_2: []}
            current_request[event_type].append(parts)
            current_type = event_type

        return ret


def partition_full_log(file_path):
    return partition_log_template(file_path, "recv", "send")


def partition_lite_log(file_path):
    return partition_log_template(file_path, "request", "response")


def analysis_vanilla(log, title, printout=True):
    parsed_log = [[] for _ in range(4)]
    for i in range(1, len(log)):  # skip server greeting
        recv = log[i]["recv"]
        send = log[i]["send"]

        if len(recv) != 4 or len(send) != 2:
            print(f"Log {i} has no recv or send, max_i: {len(log)}")
            print(f"log: {log[i]}")
            break

        first_recv_time = int(recv[0][0])
        last_recv_time = int(recv[-1][0])
        first_send_time = int(send[0][0])
        last_send_time = int(send[-1][0])

        parsed_log[0].append(first_recv_time)
        parsed_log[1].append(last_recv_time)
        parsed_log[2].append(first_send_time)
        parsed_log[3].append(last_send_time)

    parsed_log = np.array(parsed_log)

    if printout:
        avg_recv = np.mean(parsed_log[1] - parsed_log[0])
        avg_compute = np.mean(parsed_log[2] - parsed_log[1])
        avg_send = np.mean(parsed_log[3] - parsed_log[2])
        avg_lat = np.mean(parsed_log[3] - parsed_log[0])
        print(
            f"{title}(avg_lat: {avg_lat}) - avg_recv: {avg_recv}, avg_compute: {avg_compute}, avg_send: {avg_send}"
        )
    return parsed_log


def analysis_lite(log, full_parsed, title):
    lite_parsed = [[] for _ in range(6)]
    for i in range(1, len(log)):  # skip server greeting
        request = log[i]["request"]
        response = log[i]["response"]

        if len(request) != 3 or len(response) != 3:
            print(f"Log {i} has no request or response, max_i: {len(log)}")
            # print(f"log: {log[i]}")
            break

        request_pre_recv = int(request[0][0])
        request_post_recv = int(request[1][0])
        request_post_send = int(request[2][0])
        response_pre_recv = int(response[0][0])
        response_post_recv = int(response[1][0])
        response_post_send = int(response[2][0])

        lite_parsed[0].append(request_pre_recv)
        lite_parsed[1].append(request_post_recv)
        lite_parsed[2].append(request_post_send)
        lite_parsed[3].append(response_pre_recv)
        lite_parsed[4].append(response_post_recv)
        lite_parsed[5].append(response_post_send)

    # for i in range(len(lite_parsed[0])):
    #     try:
    #         assert lite_parsed[0][i] <= full_parsed[0][i]
    #     except AssertionError as e:
    #         print(f"Assert failed {e}, index {i}")
    #         full_values = ", ".join([f"{value}" for value in full_parsed[:, i]])
    #         lite_values = ", ".join([f"{value}" for value in lite_parsed[:, i]])
    #         print(f"full: {full_values}")
    #         print(f"lite: {lite_values}")
    #         raise

    lite_parsed = np.array(lite_parsed)
    cnt = min(lite_parsed.shape[1], full_parsed.shape[1])
    lite_parsed = [np.array(lite_parsed[i][:cnt]) for i in range(6)]
    full_parsed = [np.array(full_parsed[i][:cnt]) for i in range(4)]

    avg_request_lite_recv = np.mean(lite_parsed[1] - lite_parsed[0])
    avg_request_lite_send = np.mean(lite_parsed[2] - lite_parsed[1])
    avg_request_lo = np.mean(full_parsed[0] - lite_parsed[2])
    avg_request_full_recv = np.mean(full_parsed[1] - full_parsed[0])
    avg_compute = np.mean(full_parsed[2] - full_parsed[1])
    avg_response_full_send = np.mean(full_parsed[3] - full_parsed[2])
    avg_response_lo = np.mean(lite_parsed[3] - full_parsed[3])
    avg_response_lite_recv = np.mean(lite_parsed[4] - lite_parsed[3])
    avg_response_lite_send = np.mean(lite_parsed[5] - lite_parsed[4])
    avg_lat = np.mean(lite_parsed[5] - lite_parsed[0])
    print(
        f"{title}(avg_lat: {avg_lat}) - "
        f"avg_request_lite_recv: {avg_request_lite_recv}, "
        f"avg_request_lite_send: {avg_request_lite_send}, "
        f"avg_request_lo: {avg_request_lo}, "
        f"avg_request_full_recv: {avg_request_full_recv}, "
        f"avg_compute: {avg_compute}, "
        f"avg_response_full_send: {avg_response_full_send}, "
        f"avg_response_lo: {avg_response_lo}, "
        f"avg_response_lite_recv: {avg_response_lite_recv}, "
        f"avg_response_lite_send: {avg_response_lite_send}"
    )


vanilla_full = partition_full_log("vanilla/full.log")
analysis_vanilla(vanilla_full, "vanilla_full")

lite_full = partition_full_log("lite/full.log")
lite_full_parsed = analysis_vanilla(lite_full, "lite_full", False)
lite_lite = partition_lite_log("lite/lite.log")
analysis_lite(lite_lite, lite_full_parsed, "lite_lite")

no_send_full = partition_full_log("lite_while_full_does_not_send/full.log")
no_send_full_parsed = analysis_vanilla(no_send_full, "no_send_full", False)
no_send_lite = partition_lite_log("lite_while_full_does_not_send/lite.log")
analysis_lite(no_send_lite, no_send_full_parsed, "no_send_lite")

lite_no_des_full = partition_full_log("lite_no_des/full.log")
lite_no_des_full_parsed = analysis_vanilla(lite_no_des_full, "lite_no_des_full", False)
lite_no_des_lite = partition_lite_log("lite_no_des/lite.log")
analysis_lite(lite_no_des_lite, lite_no_des_full_parsed, "lite_no_des_lite")