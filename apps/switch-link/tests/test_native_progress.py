from pathlib import Path
import json
import tempfile
import time
import unittest

from switch_link.discovery import Discovery, decode_beacon
from switch_link.http import Application


class NativeProgressTests(unittest.TestCase):
    def test_malformed_task_is_ignored_without_losing_the_device_beacon(self):
        payload={'type':'switch-link','version':1,'device_id':'a'*32,'progress_port':28772,
                 'task':{'id':'b'*32,'stage':'installing','progress':float('nan')}}
        result=decode_beacon(json.dumps(payload).encode(),'127.0.0.1')
        self.assertIsNotNone(result)
        self.assertIsNone(result.get('task'))

    def test_active_transfer_beacons_update_progress_without_opening_more_ftp_connections(self):
        discovery=Discovery(auto=False)
        identity='a'*32
        discovery.devices[identity]={'id':identity,'ip':'127.0.0.1','port':5000,'connected':True,'last_seen':0}
        discovery.busy=lambda:True
        def forbidden_probe(*args):
            raise AssertionError('A transfer must not start competing FTP probes')
        discovery._probe=forbidden_probe
        discovery.observe({'host':'127.0.0.1','device_id':identity,'task':{'id':'b'*32,'stage':'installing','progress':42}})
        device=discovery.get(identity)
        self.assertTrue(device['connected'])
        self.assertEqual(device['current_task']['progress'],42)

    def test_native_percentage_does_not_overwrite_transfer_bytes_or_fake_install_completion(self):
        with tempfile.TemporaryDirectory() as directory:
            app=Application(Path(directory),discover=False)
            identity='a'*32; job_id='b'*32
            app.selected_id=identity
            app.discovery.devices[identity]={'id':identity,'connected':True,'last_seen':time.time(),
                'current_task':{'id':job_id,'source':'console','stage':'registering','determinate':False,'bytes_done':0,'bytes_total':0}}
            app.jobs.jobs[job_id]={'id':job_id,'kind':'install','state':'transferring','stage':'transferring',
                                  'bytes_done':100,'bytes_total':100,'progress':100,'updated_at':time.time()}
            try:
                result=app.status()['jobs'][0]
                self.assertEqual(result['state'],'transferring')
                self.assertEqual(result['stage'],'registering')
                self.assertEqual(result['bytes_done'],100)
                self.assertIsNone(result.get('native_progress'))
            finally:app.close()

    def test_pipelined_install_keeps_network_progress_primary_until_all_bytes_arrive(self):
        with tempfile.TemporaryDirectory() as directory:
            app=Application(Path(directory),discover=False)
            identity='a'*32; job_id='b'*32
            app.selected_id=identity
            app.discovery.devices[identity]={'id':identity,'connected':True,'last_seen':time.time(),
                'current_task':{'id':job_id,'source':'console','stage':'installing','progress':42}}
            app.jobs.jobs[job_id]={'id':job_id,'kind':'install','state':'transferring','stage':'transferring',
                                  'bytes_done':1,'bytes_total':100,'progress':1,'updated_at':time.time()}
            try:
                result=app.status()['jobs'][0]
                self.assertEqual(result['stage'],'transferring')
                self.assertEqual(result['native_progress'],42)
                app.discovery.devices[identity]['current_task']['source']='pc'
                self.assertNotIn('native_progress',app.status()['jobs'][0])
            finally:app.close()


if __name__=='__main__':unittest.main()
