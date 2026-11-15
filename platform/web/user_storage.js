// Runs before main: SDL's generic storage will operate on this persistent mount.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
    const args = Module['arguments'] || [];
    const override = args.find(value => value.startsWith('--storage-root='));
    // Mount SDL's parent directory so organization/application identities remain
    // owned by C++ configuration instead of being duplicated in JavaScript.
    const root = override ? override.substring('--storage-root='.length) : '/libsdl';
    const dependency = 'ikigai-user-storage';
    addRunDependency(dependency);
    Module['ikigaiStorageState'] = 'loading';
    let commits = Promise.resolve();
    const sync = populate => new Promise((resolve, reject) => FS.syncfs(populate, error => error ? reject(error) : resolve()));
    async function initialize() {
        FS.mkdirTree(root);
        FS.mount(IDBFS, {}, root);
        await sync(true);
        Module['ikigaiStorageCommit'] = () => {
            commits = commits.catch(() => {}).then(() => sync(false));
            commits.catch(error => {
                Module['ikigaiStorageError'] = String(error);
                (Module['printErr'] || console.error)('User storage commit failed: ' + error);
            });
            return commits;
        };
        Module['ikigaiStorageState'] = 'ready';
        removeRunDependency(dependency);
    }
    function failed(error) {
        Module['ikigaiStorageState'] = 'failed';
        Module['ikigaiStorageError'] = String(error);
        abort('Persistent user storage unavailable: ' + error);
    }
    // Keep one writer per origin/container, including during initialization.
    if (typeof navigator !== 'undefined' && navigator.locks) {
        navigator.locks.request('ikigai-storage:' + root, {ifAvailable: true}, async lock => {
            if (!lock) throw new Error('This profile storage is already open in another tab');
            await initialize();
            await new Promise(() => {}); // Browser releases the lock when this page closes.
        }).catch(failed);
    } else {
        failed(new Error('Web Locks support is required for persistent profile storage'));
    }
});
