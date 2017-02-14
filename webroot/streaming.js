
function WebSocketAudioStreamer(wsStreamUrl, audioContext) {
	var scope = this;
	this.actx = audioContext;

	this.socket = new WebSocket(wsStreamUrl);

	this.socket.onopen = function (event) {
		console.log('estabslished');
		scope.socket.send("Here's some text that the server is urgently awaiting!");
	};


	


	this.socket.onmessage = function (event) {
		var ourl = URL.createObjectURL(event.data);
		
		var xhr = new XMLHttpRequest();
		xhr.responseType = 'arraybuffer';
		xhr.open('GET', ourl, true);
		xhr.onload = function () {
			//URL.revokeObjectURL(ourl);
			scope._processStreamData(this.response);
		};
		xhr.send();
	}
}



WebSocketAudioStreamer.prototype.getInfo = function () {
	return this.color + ' ' + this.type + ' apple';
};


WebSocketAudioStreamer.prototype._receivedFrame = function () {
	return this.color + ' ' + this.type + ' apple';
};





WebSocketAudioStreamer.prototype._decodeObjectURL = function (ourl) {
	var asset = AV.Asset.fromURL(ourl);
	var buffer = asset.decodeToBuffer(function (buf) {
		console.log(buf);
	});
	console.log(buffer);
};


WebSocketAudioStreamer.prototype._processStreamData = function (data) {
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
};


var asset = AV.Asset.fromURL('s.aac');
var buffer = asset.decodeToBuffer(function (buf) {
	console.log(buf);
});

/*

function blobToUint8Array(b) {
	var uri = URL.createObjectURL(b),
		xhr = new XMLHttpRequest(),
		i,
		ui8;
	xhr.responseType = 'arraybuffer';
	xhr.open('GET', uri, true);
	xhr.onload = function () {
		var audioData = xhr.response;

		console.log(audioData.byteLength);


		audioCtx.decodeAudioData(audioData, function (buffer) {
			source.buffer = buffer;

			source.connect(audioCtx.destination);
			source.loop = true;
		},

			function (e) { "Error with decoding audio data" + e.err });


	};

	xhr.send();

	URL.revokeObjectURL(uri);

	return xhr.response;
}
*/

/*
https://github.com/kazuki/opus.js-sample
https://github.com/audiocogs/aurora.js/wiki/Getting-Started
https://github.com/kazuki/opus.js-sample
*/