
function WebSocketAudioStreamer(wsStreamUrl, audioContext) {
	var scope = this;
	this.actx = audioContext;

	this.socket = new WebSocket(wsStreamUrl);

	this.socket.onopen = function (event) {
		console.log('WS opened', event);
		scope.socket.send("START");
	};

	var decWorker = new Worker('lib/opus_decoder.js');
	this.decodeWorker = decWorker;
	
	var packetDecoded = function(ev) {
					console.log(ev.data);
				};
	this.decodeWorker.onmessage = function(ev) {
                if (ev.data.status != 0) {
					console.error('Opus header error', ev.data.reason);
                    return;
                }
				
				this.samplingRate = ev.data.sampling_rate
				this.numChannels = ev.data.num_of_channels;
	
                console.log('decodedHeader', {fs: this.samplingRate, nch: this.numChannels});
				decWorker.onmessage = packetDecoded;
            };
			

	this.messageIndex = 0;

	this.socket.binaryType = 'arraybuffer';
	this.socket.onmessage = function (event) {
		
		scope._processStreamData(event.data);
	}
}








WebSocketAudioStreamer.prototype._decodeObjectURL = function (ourl) {
	var asset = AV.Asset.fromURL(ourl);
	var buffer = asset.decodeToBuffer(function (buf) {
		console.log(buf);
	});
	console.log(buffer);
};


WebSocketAudioStreamer.prototype._processStreamData = function (data) {
	//console.log('_processStreamData', data.byteLength, data.data);
	
	
	if(this.messageIndex == 0) {
		console.log('decoding header packet');
		this.decodeWorker.postMessage({ config: {}, packets: [{data:data}]}, [data]);
	} else {
		this.decodeWorker.postMessage({data:data}, [data]);		
	}	

this.messageIndex++;	

};


//var asset = AV.Asset.fromURL('monitor.aac');
//var buffer = asset.decodeToBuffer(function (buf) {
//	console.log(buf);
//});
/* AAC:
var scope = this;
	var asset = scope.asset;
	if (!asset) {
		asset = scope.asset = AV.Asset.fromBuffer(data);
		var buffer = asset.decodeToBuffer(function (buf) {
			console.log('Decoded first frame!');
			
				asset.decoder.on('data', function(d) { if(!isNaN(d[0])) console.log(d.length, d[0]); });
			//console.log(asset.decoder,buf);
		});
	} else if (asset.decoder) {
		asset.decoder.bitstream.stream.list.append(new AV.Buffer(data));
		console.log(asset.decodePacket());
	} else {
		console.log('Dropping stream data while starting up...');
	}
	*/



/*
https://github.com/kazuki/opus.js-sample
https://github.com/audiocogs/aurora.js/wiki/Getting-Started
https://github.com/kazuki/opus.js-sample
*/