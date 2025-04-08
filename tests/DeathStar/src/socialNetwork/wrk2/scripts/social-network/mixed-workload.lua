local socket = require("socket")
local time = socket.gettime()*1000
local start_time = 0
local read_home_timeline_ratio = 0.60
local read_user_timeline_ratio = 0.30
local compose_post_ratio       = 0.10
math.randomseed(time)
math.random(); math.random(); math.random()

local charset = {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 'a', 's',
  'd', 'f', 'g', 'h', 'j', 'k', 'l', 'z', 'x', 'c', 'v', 'b', 'n', 'm', 'Q',
  'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 'A', 'S', 'D', 'F', 'G', 'H',
  'J', 'K', 'L', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '1', '2', '3', '4', '5',
  '6', '7', '8', '9', '0'}

local decset = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'}

-- load env vars
local max_user_index = tonumber(os.getenv("max_user_index")) or 962
local init_user_post_count = tonumber(os.getenv("init_user_post_count")) or 1000
local rps = tonumber(os.getenv("rps")) or 1500
local compose_post_rps = rps * compose_post_ratio
local compose_post_rps_per_user = compose_post_rps / max_user_index

-- Zipf distribution implementation
local function zeta(n, theta)
    local sum = 0
    for i = 1, n do
        sum = sum + (1 / math.pow(i, theta))
    end
    return sum
end

local function init_zipf(n, theta)
    local z = zeta(n, theta)
    local dist = {}
    for i = 1, n do
        dist[i] = (1 / math.pow(i, theta)) / z
    end
    
    -- Calculate cumulative probabilities
    local cum_prob = {}
    cum_prob[1] = dist[1]
    for i = 2, n do
        cum_prob[i] = cum_prob[i-1] + dist[i]
    end
    
    return cum_prob
end

-- Initialize Zipf distribution with theta = 0.648 to get roughly 5% users making 30% requests, assuming max_user_index is 1000
local zipf_dist = init_zipf(max_user_index, 0.648)

-- The if the duration of the experiment is about 100s, the scale factor is always close to 1
local function sample_zipf(dist, scale_factor)
  local r = math.random()
  for i = 1, #dist do
      if r <= dist[i] then
          return math.floor((i - 1) * scale_factor)
      end
  end
  return math.floor((#dist - 1) * scale_factor)
end

local function sample_zipf_start(max_val)
  return sample_zipf(zipf_dist, max_val / max_user_index)
end

local function stringRandom(length)
  if length > 0 then
    return stringRandom(length - 1) .. charset[math.random(1, #charset)]
  else
    return ""
  end
end

local function decRandom(length)
  if length > 0 then
    return decRandom(length - 1) .. decset[math.random(1, #decset)]
  else
    return ""
  end
end

local function get_random_node()
    if math.random() < 0.5 then
        return "node0:8080"
    else
        return "node1:8080"
    end
end

local function compose_post()
  local user_index = sample_zipf_start(max_user_index)
  local username = "username_" .. tostring(user_index)
  local user_id = tostring(user_index)
  local text = stringRandom(256)
  local num_user_mentions = math.random(0, 5)
  local num_urls = math.random(0, 5)
  local num_media = math.random(0, 4)
  local media_ids = '['
  local media_types = '['

  for i = 0, num_user_mentions, 1 do
    local user_mention_id
    while (true) do
      user_mention_id = math.random(0, max_user_index - 1)
      if user_index ~= user_mention_id then
        break
      end
    end
    text = text .. " @username_" .. tostring(user_mention_id)
  end

  for i = 0, num_urls, 1 do
    text = text .. " http://" .. stringRandom(64)
  end

  for i = 0, num_media, 1 do
    local media_id = decRandom(18)
    media_ids = media_ids .. "\"" .. media_id .. "\","
    media_types = media_types .. "\"png\","
  end

  media_ids = media_ids:sub(1, #media_ids - 1) .. "]"
  media_types = media_types:sub(1, #media_types - 1) .. "]"

  local method = "POST"
  local path = "http://" .. get_random_node() .. "/wrk2-api/post/compose"
  local headers = {}
  local body
  headers["Content-Type"] = "application/x-www-form-urlencoded"
  if num_media then
    body   = "username=" .. username .. "&user_id=" .. user_id ..
        "&text=" .. text .. "&media_ids=" .. media_ids ..
        "&media_types=" .. media_types .. "&post_type=0"
  else
    body   = "username=" .. username .. "&user_id=" .. user_id ..
        "&text=" .. text .. "&media_ids=" .. "&post_type=0"
  end

  return wrk.format(method, path, headers, body)
end

local function read_user_timeline()
    local user_id = tostring(sample_zipf_start(max_user_index))
    local time_past_exp_start = socket.gettime() - start_time
    local max_start = 1000 + compose_post_rps_per_user * time_past_exp_start
    local start = tostring(sample_zipf_start(max_start))
    local stop = tostring(start + 10)

    local args = "user_id=" .. user_id .. "&start=" .. start .. "&stop=" .. stop
    local method = "GET"
    local headers = {}
    headers["Content-Type"] = "application/x-www-form-urlencoded"
    local path = "http://" .. get_random_node() .. "/wrk2-api/user-timeline/read?" .. args
    return wrk.format(method, path, headers, nil)
end

local function read_home_timeline()
    local user_id = tostring(sample_zipf_start(max_user_index))
    local time_past_exp_start = socket.gettime() - start_time
    local max_start = 1000 + compose_post_rps_per_user * time_past_exp_start
    local start = tostring(sample_zipf_start(max_start))
    local stop = tostring(start + 10)

    local args = "user_id=" .. user_id .. "&start=" .. start .. "&stop=" .. stop
    local method = "GET"
    local headers = {}
    headers["Content-Type"] = "application/x-www-form-urlencoded"
    local path = "http://" .. get_random_node() .. "/wrk2-api/home-timeline/read?" .. args
    return wrk.format(method, path, headers, nil)
end

request = function()
    if start_time == 0 then
      start_time = socket.gettime()
    end

    cur_time = math.floor(socket.gettime())

    local coin = math.random()
    if coin < read_home_timeline_ratio then
      return read_home_timeline()
    elseif coin < read_home_timeline_ratio + read_user_timeline_ratio then
      return read_user_timeline()
    else
      return compose_post()
    end
  end
