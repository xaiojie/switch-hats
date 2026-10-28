import io
import json
import time
import unittest
import test_api


class NativeHttpApiTests(unittest.TestCase):
    setUp=test_api.ApiTests.setUp
    tearDown=test_api.ApiTests.tearDown
    request=test_api.ApiTests.request

    def native(self):
        device={'id':'a'*32,'ip':'127.0.0.1','port':5000,'mode':'switch-link','connected':True,
                'last_seen':time.time(),'http_install_supported':True,'http_install_ready':True,
                'command_port':28772,'http_formats':['nsp','xci']}
        self.app.discovery.devices[device['id']]=device
        self.app.selected_id=device['id']
        self.app.http_jobs.configure('http://127.0.0.1:1')
        self.app.library.receive('owned.nsp',io.BytesIO(b'owned'),5)
        return device

    def test_web_install_queues_native_http_job_and_never_ftp_install(self):
        device=self.native()
        code,data=self.request('POST','/api/http-install',json.dumps({'name':'owned.nsp','expected_device':device['id']}))
        self.assertEqual(code,202)
        self.assertEqual(data['job']['transport'],'http')
        self.assertEqual(self.app.jobs.snapshot(),[])
        self.assertEqual(len(self.app.status()['jobs']),1)
        code,_=self.request('GET','/api/files?path=/&expected_device='+device['id'])
        self.assertEqual(code,409,'file operations cannot overlap an installation')
        code,_=self.request('POST','/api/connect',json.dumps({'id':'other'}))
        self.assertEqual(code,409)

    def test_stale_device_and_dbi_cannot_receive_native_install(self):
        device=self.native()
        code,_=self.request('POST','/api/http-install',json.dumps({'name':'owned.nsp','expected_device':'old'}))
        self.assertEqual(code,409)
        self.app.discovery.devices[device['id']]['mode']='dbi'
        code,data=self.request('POST','/api/http-install',json.dumps({'name':'owned.nsp','expected_device':device['id']}))
        self.assertEqual(code,409)
        self.assertIn('Switch Link',data['error'])
        self.assertEqual(self.app.http_jobs.snapshot(),[])

    def test_sd_upload_cannot_overlap_a_native_http_install(self):
        device=self.native()
        self.app.http_jobs.submit(device,'owned.nsp')
        code,_=self.request('PUT','/api/files/upload?path=/&name=owned.txt&expected_device='+device['id'],b'test')
        self.assertEqual(code,409)
        self.assertEqual(self.app.jobs.snapshot(),[])
