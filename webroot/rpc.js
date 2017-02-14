
var rpcHost = (!window.location.hostname) ? "localhost" : window.location.hostname;

if (window.location.hash)
    rpcHost = window.location.hash.substr(1);


var rpc_id = 0;
var rpc = function (method, params, callback) {
    if (params && 'object' != typeof params) {
        console.error('RPC params must be passed as object with named keys!');
        callback(false);
        return;
    }

    rpc_id++;

    var rpcId = rpc_id;
    params.clientId = "browser-?@";
    $.ajax({
        url: "http://" + ((params && params['hostname']) || rpcHost) + ':26080',
        method: "POST",
        contentType: 'text/plain',
        data: JSON.stringify({ "id": (rpcId), method: method, params: params })
    })
        .done(function (data) {
            if (data.id != rpcId) {
                console.error('RPC id mistmatch', rpcId, data.id);
                callback(false);
                return;
            }

            if (data.error) {
                console.error('RPC error for', method, ':', data.error.message);
                callback(false);
                return;
            }
            callback(data.result);
        })
        .fail(function (qXHR, textStatus, errorThrown) {
            console.error('RPC failed', { method: method, params: params }, textStatus, errorThrown, qXHR.responseText)
            callback(false);
        });
};
