import io
import json
from pathlib import Path
import tempfile
import unittest
from urllib.request import build_opener, ProxyHandler, Request
from urllib.error import HTTPError

from switch_link.library import Library, HttpShare
from switch_link.http_jobs import HttpInstallJobs, HttpJobError


class HttpInstallTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.library = Library(Path(self.tmp.name)/'library')
        self.library.receive('owned.nsp', io.BytesIO(b'0123456789'), 10)
        self.sent = []
        self.device = {'id':'a'*32,'ip':'127.0.0.1','mode':'switch-link','connected':True,
            'http_install_supported':True,'http_install_ready':True,'command_port':28772,
            'http_formats':['nsp','xci']}
        self.jobs = HttpInstallJobs(self.library, sender=lambda packet,peer:self.sent.append((json.loads(packet),peer)), auto=False)
        self.share = HttpShare(self.library,host='127.0.0.1',port=0,advertise='127.0.0.1')
        self.share.link_jobs = self.jobs
        self.share.start()
        self.jobs.configure(self.share.url)
        self.opener=build_opener(ProxyHandler({}))

    def tearDown(self):
        self.jobs.close();self.share.close();self.tmp.cleanup()

    def start(self):
        job=self.jobs.submit(self.device,'owned.nsp');self.jobs.tick()
        return job,self.sent[-1][0]

    def receipt(self,job):
        return {'id':job['id'],'device_id':self.device['id'],'filename':job['id']+'_owned.nsp',
                'success':True,'result':0,'registered_titles':1,'message':'registered'}

    def post(self,url,payload):
        return self.opener.open(Request(url,data=json.dumps(payload).encode(),headers={'Content-Type':'application/json'}),timeout=3)

    def test_command_is_http_bound_to_selected_device_and_retries_reuse_job_id(self):
        job,command=self.start()
        self.assertEqual(command['type'],'switch-link-install')
        self.assertEqual(command['device_id'],self.device['id'])
        self.assertIn('/link-files/'+job['id']+'/',command['url'])
        self.assertEqual(self.sent[-1][1],('127.0.0.1',28772))
        self.jobs.tick(force=True)
        self.assertEqual(self.sent[-1][0]['id'],job['id'])
        self.assertNotIn(command['result_url'].rsplit('/',1)[-1],json.dumps(self.jobs.snapshot()))

    def test_command_uses_route_to_console_instead_of_default_internet_adapter(self):
        self.jobs.configure('http://10.8.0.2:'+str(self.share.port))
        job,command=self.start()
        self.assertTrue(command['url'].startswith('http://127.0.0.1:'+str(self.share.port)+'/'))
        with self.opener.open(command['url'],timeout=3) as r:self.assertEqual(r.read(),b'0123456789')

    def test_http_delivery_and_native_percent_do_not_confirm_installation(self):
        job,command=self.start()
        with self.opener.open(command['url'],timeout=3) as r:self.assertEqual(r.read(),b'0123456789')
        self.assertNotEqual(self.jobs.snapshot()[0]['state'],'completed')
        self.jobs.observe(dict(self.device,device_id=self.device['id'],host=self.device['ip'],task={
            'id':job['id'],'source':'console','stage':'completed','progress':100,'bytes_done':10,'bytes_total':10,'speed':0}))
        self.assertNotEqual(self.jobs.snapshot()[0]['state'],'completed')
        with self.post(command['result_url'],self.receipt(job)) as r:self.assertEqual(r.status,200)
        self.assertEqual(self.jobs.snapshot()[0]['state'],'completed')
        with self.post(command['result_url'],self.receipt(job)) as r:self.assertEqual(r.status,200)
        self.assertEqual(len(self.jobs.snapshot()),1)

    def test_capability_is_not_exposed_in_public_http_status_after_a_download(self):
        job,command=self.start()
        with self.opener.open(command['url'],timeout=3) as r:r.read()
        with self.opener.open(self.share.url+'/api/status',timeout=3) as r:
            status=r.read().decode()
        self.assertNotIn(command['result_url'].rsplit('/',1)[-1],status)

    def test_receipt_requires_matching_token_device_name_and_registered_record(self):
        job,command=self.start()
        wrong=command['result_url'][:-1]+('0' if command['result_url'][-1]!='0' else '1')
        with self.assertRaises(HTTPError) as error:self.post(wrong,self.receipt(job))
        self.assertEqual(error.exception.code,403)
        for key,value in [('device_id','b'*32),('filename','other.nsp')]:
            bad=self.receipt(job);bad[key]=value
            with self.subTest(key=key),self.assertRaises(HTTPError):self.post(command['result_url'],bad)
        bad=self.receipt(job);bad['registered_titles']=0
        with self.post(command['result_url'],bad) as r:self.assertEqual(r.status,200)
        self.assertEqual(self.jobs.snapshot()[0]['state'],'failed')

    def test_old_or_busy_clients_and_unsupported_formats_cannot_start(self):
        for change in [{'http_install_supported':False},{'mode':'dbi'},{'http_install_ready':False},{'connected':False}]:
            with self.subTest(change=change),self.assertRaises(HttpJobError):self.jobs.submit(dict(self.device,**change),'owned.nsp')
        self.library.receive('owned.nsz',io.BytesIO(b'x'),1)
        with self.assertRaises(HttpJobError):self.jobs.submit(self.device,'owned.nsz')
        self.assertEqual(self.sent,[])

    def test_queued_second_job_waits_for_native_receipt_and_active_job_cannot_be_cancelled(self):
        first,command=self.start()
        second=self.jobs.submit(self.device,'owned.nsp')
        self.jobs.tick(force=True)
        self.assertTrue(all(packet['id']==first['id'] for packet,_ in self.sent))
        with self.assertRaises(HttpJobError):self.jobs.cancel(first['id'])
        with self.post(command['result_url'],self.receipt(first)):pass
        self.jobs.tick(force=True)
        self.assertEqual(self.sent[-1][0]['id'],second['id'])
        self.assertTrue((self.library.root/'owned.nsp').exists())
