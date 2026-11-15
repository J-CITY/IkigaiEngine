mergeInto(LibraryManager.library, {
    $IkigaiWs__deps: ['$stringToNewUTF8', 'malloc', 'free', 'ikigai_ws_cancelled', 'ikigai_ws_done'],
    $IkigaiWs: {
        nextId: 1,
        sockets: {},
        finish: function (state, operation, code, error, bytes, text) {
            if (operation.finished) return;
            operation.finished = true;
            clearInterval(operation.poll); clearTimeout(operation.timeout);
            state.pending.delete(operation);
            if (state.reader === operation) state.reader = null;
            const errorPointer = error ? stringToNewUTF8(error) : 0;
            const memory = bytes && bytes.length ? _malloc(bytes.length) : 0;
            if (memory) HEAPU8.set(bytes, memory);
            _ikigai_ws_done(operation.pointer, code || (bytes && bytes.length && !memory ? 1 : 0), errorPointer, memory, memory ? bytes.length : 0, text ? 1 : 0);
            if (memory) _free(memory);
            if (errorPointer) _free(errorPointer);
        },
        fail: function (state, message) {
            for (const operation of Array.from(state.pending)) IkigaiWs.finish(state, operation, 1, message);
            if (state.socket) state.socket.close();
        }
    },
    ikigai_web_ws_create__deps: ['$IkigaiWs'],
    ikigai_web_ws_create: function () {
        const id = IkigaiWs.nextId++;
        IkigaiWs.sockets[id] = {socket: null, pending: new Set(), messages: [], reader: null};
        return id;
    },
    ikigai_web_ws_destroy__deps: ['$IkigaiWs'],
    ikigai_web_ws_destroy: function (id) {
        const state = IkigaiWs.sockets[id];
        if (state) { IkigaiWs.fail(state, 'WebSocket client destroyed'); delete IkigaiWs.sockets[id]; }
    },
    ikigai_web_ws_operation__deps: ['$IkigaiWs', '$UTF8ToString'],
    ikigai_web_ws_operation: function (id, kind, pointer, data, size, text, milliseconds) {
        const state = IkigaiWs.sockets[id];
        const operation = {pointer, kind, finished: false};
        state.pending.add(operation);
        const done = (code, error, bytes, isText) => IkigaiWs.finish(state, operation, code, error, bytes, isText);
        operation.timeout = setTimeout(() => { done(2, 'WebSocket timeout'); IkigaiWs.fail(state, 'WebSocket timeout'); }, milliseconds);
        operation.poll = setInterval(() => {
            if (_ikigai_ws_cancelled(pointer)) { done(3, 'WebSocket cancelled'); IkigaiWs.fail(state, 'WebSocket cancelled'); }
        }, 10);
        try {
            if (kind === 0) {
                if (state.socket) throw new Error('WebSocket already initialized');
                const socket = new WebSocket(UTF8ToString(data, size));
                state.socket = socket; socket.binaryType = 'arraybuffer';
                socket.onopen = () => done(0);
                socket.onerror = () => IkigaiWs.fail(state, 'WebSocket transport error');
                socket.onclose = () => {
                    for (const pending of Array.from(state.pending)) {
                        IkigaiWs.finish(state, pending, pending.kind === 3 ? 0 : 1, pending.kind === 3 ? null : 'WebSocket closed');
                    }
                };
                socket.onmessage = event => {
                    const isText = typeof event.data === 'string';
                    const bytes = isText ? new TextEncoder().encode(event.data) : new Uint8Array(event.data);
                    if (bytes.length > 1048576 || state.messages.length >= 64) { IkigaiWs.fail(state, 'WebSocket receive queue limit exceeded'); return; }
                    if (state.reader) IkigaiWs.finish(state, state.reader, 0, null, bytes, isText);
                    else state.messages.push({bytes, text: isText});
                };
            } else if (kind === 1) {
                if (!state.socket || state.socket.readyState !== WebSocket.OPEN) throw new Error('WebSocket is not connected');
                if (state.reader) throw new Error('WebSocket read already pending');
                const message = state.messages.shift();
                if (message) done(0, null, message.bytes, message.text);
                else state.reader = operation;
            } else if (kind === 2) {
                if (!state.socket || state.socket.readyState !== WebSocket.OPEN) throw new Error('WebSocket is not connected');
                if (size > 1048576 || state.socket.bufferedAmount + size > 1048576) throw new Error('WebSocket send queue limit exceeded');
                if (Array.from(state.pending).some(pending => pending !== operation && pending.kind === 2)) throw new Error('WebSocket write already pending');
                const copy = HEAPU8.slice(data, data + size);
                state.socket.send(text ? new TextDecoder('utf-8', {fatal: true}).decode(copy) : copy);
                clearInterval(operation.poll);
                operation.poll = setInterval(() => {
                    if (_ikigai_ws_cancelled(pointer)) { done(3, 'WebSocket cancelled'); IkigaiWs.fail(state, 'WebSocket cancelled'); }
                    else if (!state.socket.bufferedAmount) done(0);
                }, 10);
            } else {
                if (!state.socket || state.socket.readyState === WebSocket.CLOSED) done(0);
                else state.socket.close(1000);
            }
        } catch (error) { done(1, String(error)); }
    },
    ikigai_web_http__deps: ['$UTF8ToString', '$stringToNewUTF8', 'malloc', 'free',
        'ikigai_http_cancelled', 'ikigai_http_headers', 'ikigai_http_body', 'ikigai_http_done'],
    ikigai_web_http: function (pointer, urlPointer, headersPointer, timeoutSeconds, maxBodyBytes) {
        const url = UTF8ToString(urlPointer);
        const headers = JSON.parse(UTF8ToString(headersPointer));
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort('HTTP timeout'), timeoutSeconds * 1000);
        const cancellation = setInterval(() => {
            if (_ikigai_http_cancelled(pointer)) controller.abort('Transfer cancelled');
        }, 10);
        (async () => {
            let errorPointer = 0;
            let reader;
            try {
                const response = await fetch(url, {headers, signal: controller.signal, redirect: 'error'});
                // Native downloads require identity encoding; browser Fetch returns decoded bytes.
                const visibleHeaders = Object.fromEntries(response.headers.entries());
                delete visibleHeaders['content-encoding'];
                const metadata = stringToNewUTF8(JSON.stringify(visibleHeaders));
                let rejected;
                try { rejected = _ikigai_http_headers(pointer, response.status, metadata); }
                finally { _free(metadata); }
                if (rejected) throw new Error('HTTP header handler failed');
                let total = 0;
                if (response.body) {
                    reader = response.body.getReader();
                    while (true) {
                        const part = await reader.read();
                        if (part.done) break;
                        total += part.value.byteLength;
                        if (total > maxBodyBytes) throw new Error('HTTP body limit exceeded');
                        for (let offset = 0; offset < part.value.byteLength; offset += 65536) {
                            const chunk = part.value.subarray(offset, offset + 65536);
                            const memory = _malloc(chunk.byteLength);
                            if (!memory) throw new Error('HTTP buffer allocation failed');
                            try {
                                HEAPU8.set(chunk, memory);
                                if (_ikigai_http_body(pointer, memory, chunk.byteLength)) throw new Error('HTTP body handler failed');
                            } finally { _free(memory); }
                        }
                    }
                }
            } catch (error) {
                controller.abort();
                if (reader) { try { await reader.cancel(); } catch (_) {} }
                errorPointer = stringToNewUTF8(String(error));
            } finally {
                clearTimeout(timeout); clearInterval(cancellation);
                _ikigai_http_done(pointer, errorPointer);
                if (errorPointer) _free(errorPointer);
            }
        })();
    }
});
