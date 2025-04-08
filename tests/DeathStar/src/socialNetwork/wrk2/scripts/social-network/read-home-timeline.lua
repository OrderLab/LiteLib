local socket = require("socket")
local time = socket.gettime()*1000
math.randomseed(time)
math.random(); math.random(); math.random()

-- load env vars
local max_user_index = tonumber(os.getenv("max_user_index")) or 962

local function get_random_node()
  if math.random() < 0.5 then
      return "node0:8080"
  else
      return "node1:8080"
  end
end

request = function()
  local user_id = tostring(math.random(0, max_user_index - 1))
  local start = tostring(math.random(0, 1000))
  local stop = tostring(start + 10)

  local args = "user_id=" .. user_id .. "&start=" .. start .. "&stop=" .. stop
  local method = "GET"
  local headers = {}
  headers["Content-Type"] = "application/x-www-form-urlencoded"
  local path = "http://" .. get_random_node() .. "/wrk2-api/home-timeline/read?" .. args
  return wrk.format(method, path, headers, nil)

end
